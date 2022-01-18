#pragma once

#include <chainbase/object.hpp>
#include <chainbase/undo_index.hpp>
#include <chainbase/allocator.hpp>
#include <chainbase/detail/database.hpp>

#include <boost/throw_exception.hpp>

#include <vector>
#include <memory>
#include <stdexcept>
#include <filesystem>

namespace chainbase {
	namespace fs = std::filesystem;

	template<class T, class A, class... I>
   class undo_index;

	namespace detail {
		template<class MultiIndexType>
		struct get_multi_index_impl;

		template<class T, class A, class... I>
		struct get_multi_index_impl<undo_index<T, A, I...>> {
			using type = undo_index<T, A, I...>;
		};

		template<class MultiIndexType>
		using get_multi_index = typename get_multi_index_impl<MultiIndexType>::type;
	}

	/**
	 *  Database for multi_index containers
	 */
	class database {
	public:
		enum open_flags { read_only = 0, read_write = 1 };

		database(const fs::path& dir, open_flags write = read_only, uint64_t db_file_size = 0, bool allow_dirty = false);
		~database();
		database(database&&) = default;
		database& operator=(database&&) = default;

		bool is_read_only() const
		{
			return _read_only;
		}

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
			constexpr auto type_id = detail::get_multi_index<MultiIndexType>::value_type::type_id;
			using index_type = detail::get_multi_index<MultiIndexType>;
			using index_alloc = typename index_type::allocator_type;

			if (!(_index_map.size() <= type_id.index || _index_map[type_id.index] == nullptr)) {
				BOOST_THROW_EXCEPTION(std::logic_error(type_id.str() + " is already in use"));
			}

			index_type* idx_ptr = _db_file.get_segment_manager()->find<index_type>(type_id.c_str).first;
			bool first_time_adding = false;
			if (!idx_ptr) {
				if (_read_only) {
					BOOST_THROW_EXCEPTION(std::runtime_error("unable to find index for " + type_id.str() + " in read only database"));
				}
				first_time_adding = true;
				idx_ptr = _db_file.get_segment_manager()->construct<index_type>(type_id.c_str)(index_alloc(_db_file.get_segment_manager()));
			}

			idx_ptr->validate();

			// Ensure the undo stack of added index is consistent with the other indices in the database
			if (_index_list.size() > 0) {
				auto expected_revision_range = _index_list.front()->undo_stack_revision_range();
				auto added_index_revision_range = idx_ptr->undo_stack_revision_range();

				if (added_index_revision_range.first != expected_revision_range.first || added_index_revision_range.second != expected_revision_range.second) {
					if (!first_time_adding) {
						BOOST_THROW_EXCEPTION(std::logic_error(
						    "existing index for " + type_id.str() + " has an undo stack (revision range [" + std::to_string(added_index_revision_range.first) +
						    ", " + std::to_string(added_index_revision_range.second) +
						    "]) that is inconsistent with other indices in the database (revision range [" + std::to_string(expected_revision_range.first) +
						    ", " + std::to_string(expected_revision_range.second) + "]); corrupted database?"));
					}

					if (_read_only) {
						BOOST_THROW_EXCEPTION(
						    std::logic_error("new index for " + type_id.str() +
						                     " requires an undo stack that is consistent with other indices in the database; cannot fix in read-only mode"));
					}

					idx_ptr->set_revision(static_cast<uint64_t>(expected_revision_range.first));
					while (idx_ptr->revision() < expected_revision_range.second) {
						idx_ptr->start_undo_session().push();
					}
				}
			}

			if (type_id.index >= _index_map.size())
				_index_map.resize(type_id.index + 1);

			auto new_index = std::make_unique<detail::multi_index<index_type>>(*idx_ptr);
			_index_list.push_back(new_index.get());
			_index_map[type_id.index] = std::move(new_index);
		}

		auto get_segment_manager()
		{
			return _db_file.get_segment_manager();
		}

		auto get_segment_manager() const
		{
			return _db_file.get_segment_manager();
		}

		size_t get_free_memory() const
		{
			return _db_file.get_segment_manager()->get_free_memory();
		}

		template<class MultiIndexType>
		detail::get_multi_index<MultiIndexType> const& get_index() const
		{
			typedef detail::get_multi_index<MultiIndexType> index_type;
			typedef index_type* index_type_ptr;
			assert(_index_map.size() > index_type::value_type::type_id.index);
			assert(_index_map[index_type::value_type::type_id.index]);
			return *index_type_ptr(_index_map[index_type::value_type::type_id.index]->get());
		}

		template<class MultiIndexType, class ByIndex>
		auto get_index() const -> decltype(std::declval<detail::get_multi_index<MultiIndexType> const>().indices().template get<ByIndex>())
		{
			typedef detail::get_multi_index<MultiIndexType> index_type;
			typedef index_type* index_type_ptr;
			assert(_index_map.size() > index_type::value_type::type_id.index);
			assert(_index_map[index_type::value_type::type_id.index]);
			return index_type_ptr(_index_map[index_type::value_type::type_id.index]->get())->indices().template get<ByIndex>();
		}

		template<class MultiIndexType>
		detail::get_multi_index<MultiIndexType>& get_mutable_index()
		{
			typedef detail::get_multi_index<MultiIndexType> index_type;
			typedef index_type* index_type_ptr;
			assert(_index_map.size() > index_type::value_type::type_id.index);
			assert(_index_map[index_type::value_type::type_id.index]);
			return *index_type_ptr(_index_map[index_type::value_type::type_id.index]->get());
		}

	private:
		detail::pinnable_mapped_file _db_file;
		bool _read_only = false;

		std::vector<detail::abstract_multi_index*> _index_list;
		std::vector<std::unique_ptr<detail::abstract_multi_index>> _index_map;
	};
}
