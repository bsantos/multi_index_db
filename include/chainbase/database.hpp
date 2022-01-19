#pragma once

#include <chainbase/allocator.hpp>
#include <chainbase/detail/database.hpp>

#include <boost/throw_exception.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <vector>
#include <memory>
#include <stdexcept>
#include <filesystem>

namespace chainbase {
	namespace bip = boost::interprocess;
	namespace fs = std::filesystem;

	/**
	 *  Database open mode
	 */
	enum class open_mode {
		read_only,
		read_write
	};

	/**
	 *  Database open action to be applied if dirty flag is set
	 *
	 *  What action should be applied on open if dirty flag is set.
	 *   - fail:  fails with an error
	 *   - reset:
	 *   - allow: does nothing
	 */
	enum class dirty_action {
		fail,  /// fail with an error/exception
		allow, /// do nothing
		reset, /// reset the database to a clean state over the existing one
	};

	/**
	 *  Database open outcome
	 *
	 *  The state of database after opening
	 */
	enum class open_outcome {
		good,      /// database file was opened with no issues
		created,   /// a new database file was created
		corrupted, /// database file dirty flag was set, data might be corrupted, proceed at your own risk
		reset,     /// database file dirty flag was set, data has been reset to a clean state
	};

	/**
	 *  Database for multi_index containers
	 */
	class database {
	public:
		database(fs::path const& fpath, open_mode mode = open_mode::read_only, uint64_t db_file_size = 0, dirty_action action = dirty_action::fail);
		~database() noexcept;

		database(database const&) = delete;
		database& operator=(database const&) = delete;

		database(database&&) noexcept;
		database& operator=(database&&) noexcept;

		bool is_read_only() const { return _mode == open_mode::read_only; }
		bool was_created() const { return _outcome == open_outcome::created || _outcome == open_outcome::reset; }
		bool is_corrupted() const { return _outcome == open_outcome::corrupted; }
		bool was_corrupted() const { return _outcome == open_outcome::reset; }

		open_mode mode() const { return _mode; }
		open_outcome outcome() const { return _outcome; }

		struct session {
		public:
			session(session&& s)
			    : _index_sessions { std::move(s._index_sessions) }
			{}
			session(std::vector<std::unique_ptr<detail::abstract_undo_session>>&& s)
			    : _index_sessions { std::move(s) }
			{}

			~session()
			{
				undo();
			}

			void push()
			{
				for (auto& i : _index_sessions)
					i->push();

				_index_sessions.clear();
			}

			void squash()
			{
				for (auto& i : _index_sessions)
					i->squash();

				_index_sessions.clear();
			}

			void undo()
			{
				for (auto& i : _index_sessions)
					i->undo();
				_index_sessions.clear();
			}

		private:
			friend class database;
			session() {}

			std::vector<std::unique_ptr<detail::abstract_undo_session>> _index_sessions;
		};

		session start_undo_session();

		int64_t revision() const
		{
			if (_index_list.size() == 0)
				return -1;
			return _index_list[0]->revision();
		}

		void undo();
		void squash();
		void commit(int64_t revision);
		void undo_all();

		void set_revision(uint64_t revision)
		{
			for (auto i : _index_list)
				i->set_revision(revision);
		}

		template<typename MultiIndexType>
		void add_index()
		{
			using index_type = detail::get_multi_index<MultiIndexType>;
			using index_alloc = typename index_type::allocator_type;

			constexpr auto type_id = index_type::value_type::type_id;
			constexpr auto type_name = index_type::value_type::type_name;

			if (!(_index_map.size() <= type_id || _index_map[type_id] == nullptr)) {
				BOOST_THROW_EXCEPTION(std::logic_error(type_name.str() + " is already in use"));
			}

			index_type* idx_ptr = _segment_manager->find<index_type>(type_name.c_str()).first;
			bool first_time_adding = false;
			if (!idx_ptr) {
				if (is_read_only()) {
					BOOST_THROW_EXCEPTION(std::runtime_error("unable to find index for " + type_name.str() + " in read only database"));
				}
				first_time_adding = true;
				idx_ptr = _segment_manager->construct<index_type>(type_name.c_str())(index_alloc(_segment_manager));
			}

			idx_ptr->validate();

			// Ensure the undo stack of added index is consistent with the other indices in the database
			if (_index_list.size() > 0) {
				auto expected_revision_range = _index_list.front()->undo_stack_revision_range();
				auto added_index_revision_range = idx_ptr->undo_stack_revision_range();

				if (added_index_revision_range.first != expected_revision_range.first || added_index_revision_range.second != expected_revision_range.second) {
					if (!first_time_adding) {
						BOOST_THROW_EXCEPTION(std::logic_error(
						    "existing index for " + type_name.str() + " has an undo stack (revision range [" + std::to_string(added_index_revision_range.first) +
						    ", " + std::to_string(added_index_revision_range.second) +
						    "]) that is inconsistent with other indices in the database (revision range [" + std::to_string(expected_revision_range.first) +
						    ", " + std::to_string(expected_revision_range.second) + "]); corrupted database?"));
					}

					if (is_read_only()) {
						BOOST_THROW_EXCEPTION(
						    std::logic_error("new index for " + type_name.str() +
						                     " requires an undo stack that is consistent with other indices in the database; cannot fix in read-only mode"));
					}

					idx_ptr->set_revision(static_cast<uint64_t>(expected_revision_range.first));
					while (idx_ptr->revision() < expected_revision_range.second) {
						idx_ptr->start_undo_session().push();
					}
				}
			}

			if (type_id >= _index_map.size())
				_index_map.resize(type_id + 1);

			auto new_index = std::make_unique<detail::multi_index<index_type>>(*idx_ptr);
			_index_list.push_back(new_index.get());
			_index_map[type_id] = std::move(new_index);
		}

		segment_manager* get_segment_manager()
		{
			return _segment_manager;
		}

		segment_manager const* get_segment_manager() const
		{
			return _segment_manager;
		}

		size_t get_free_memory() const
		{
			return _segment_manager->get_free_memory();
		}

		size_t get_used_memory() const
		{
			return get_segment_size() - get_free_memory();
		}

		size_t get_segment_size() const
		{
			return _segment_manager->get_size();
		}

		template<class MultiIndexType>
		detail::get_multi_index<MultiIndexType> const& get_index() const
		{
			typedef detail::get_multi_index<MultiIndexType> index_type;
			typedef index_type* index_type_ptr;
			assert(_index_map.size() > index_type::value_type::type_id);
			assert(_index_map[index_type::value_type::type_id]);
			return *index_type_ptr(_index_map[index_type::value_type::type_id]->get());
		}

		template<class MultiIndexType, class ByIndex>
		auto get_index() const -> decltype(std::declval<detail::get_multi_index<MultiIndexType> const>().indices().template get<ByIndex>())
		{
			typedef detail::get_multi_index<MultiIndexType> index_type;
			typedef index_type* index_type_ptr;
			assert(_index_map.size() > index_type::value_type::type_id);
			assert(_index_map[index_type::value_type::type_id]);
			return index_type_ptr(_index_map[index_type::value_type::type_id]->get())->indices().template get<ByIndex>();
		}

		template<class MultiIndexType>
		detail::get_multi_index<MultiIndexType>& get_mutable_index()
		{
			typedef detail::get_multi_index<MultiIndexType> index_type;
			typedef index_type* index_type_ptr;
			assert(_index_map.size() > index_type::value_type::type_id);
			assert(_index_map[index_type::value_type::type_id]);
			return *index_type_ptr(_index_map[index_type::value_type::type_id]->get());
		}

	private:
		void flush();
		void dirty();
		bool dirty() const;

	private:
		segment_manager* _segment_manager { nullptr };

		open_mode _mode;
		open_outcome _outcome;
		fs::path _file_path;
		bip::file_lock _file_lock;
		bip::file_mapping _file_mapping;
		bip::mapped_region _file_mapped_region;

		std::vector<detail::abstract_multi_index*> _index_list;
		std::vector<std::unique_ptr<detail::abstract_multi_index>> _index_map;
	};
}
