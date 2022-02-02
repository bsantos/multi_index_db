#pragma once

#include <chainbase/allocator.hpp>
#include <chainbase/container.hpp>
#include <chainbase/detail/container.hpp>

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

		template<class T>
		auto get() -> std::enable_if_t<is_container_v<T>, T*>
		{
			using container_type = detail::container<T>;
			using container_alloc = typename container_type::allocator_type;

			constexpr auto type_name = container_type::value_type::type_name;

			container_type* ptr = _segment_manager->find<container_type>(type_name.c_str()).first;
			if (!ptr)
				ptr = _segment_manager->construct<container_type>(type_name.c_str())(container_alloc(_segment_manager));

			return ptr->get();
		}

		template<class T>
		auto get() const -> std::enable_if_t<is_container_v<T>, T const*>
		{
			using container_type = detail::container<T>;

			constexpr auto type_name = container_type::value_type::type_name;

			container_type* ptr = _segment_manager->find<container_type>(type_name.c_str()).first;
			return ptr ? ptr->get() : nullptr;
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
	};
}
