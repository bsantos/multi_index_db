#include <chainbase/error.hpp>
#include <chainbase/database.hpp>
#include <chainbase/environment.hpp>

#include <fstream>
#include <string_view>

namespace chainbase {
	namespace ba = boost::archive;
	namespace bs = boost::serialization;

	namespace {
		constexpr size_t db_min_free = 2046;
		constexpr size_t db_min_size = 4096;

		static_assert(db_min_size > header_size + sizeof(segment_manager) + db_min_free, "review DB minimum size");

		void touch(fs::path const& fpath)
		{
			std::ofstream ofs { fpath, std::ofstream::trunc };
		}

		[[noreturn]]
		void throw_error(fs::path const& fpath, errc error_code, std::string_view reason)
		{
			auto name = fpath.string();
			std::string what;

			what.reserve(12 + name.size() + reason.size());

			what += '\"';
			what += name;
			what += "\" database ";
			what += reason;

			BOOST_THROW_EXCEPTION(std::system_error(make_error_code(error_code), what));
		}

		bool validate_db_header(fs::path const& fpath)
		{
			std::ifstream hs(fpath.generic_string(), std::ifstream::binary);
			char header[header_size];

			hs.read(header, header_size);
			if (hs.fail())
				throw_error(fpath, errc::bad_header, "invalid header");

			auto dbheader = reinterpret_cast<db_header const*>(header);

			if (dbheader->id != header_id || dbheader->size != header_size)
				throw_error(fpath, errc::incorrect_db_version, "incompatible version");

			if (dbheader->dbenviron != environment())
				throw_error(fpath, errc::incompatible, "was created on a different environment:\n" + dbheader->dbenviron.str());

			return !dbheader->dirty;
		}

		template<class T = void>
		T* get_address_at_offset(bip::mapped_region const& mapping, size_t offset)
		{
			void* addr = static_cast<char*>(mapping.get_address()) + offset;
			return static_cast<T*>(addr);
		}
	}

	database::database(fs::path const& fpath, open_mode mode, uint64_t db_file_size, dirty_action action)
		: _file_path { fpath }
		, _mode { mode }
	{
		bool const writable = mode == open_mode::read_write;
		bool const file_exists = fs::exists(fpath);

		if (!writable && !file_exists)
			throw_error(fpath, errc::not_found, "file not found");

		if (file_exists) {
			_outcome = open_outcome::good;

			if (!validate_db_header(fpath)) {
				switch (action) {
				case dirty_action::allow:
					_outcome = open_outcome::corrupted;
					break;

				case dirty_action::reset:
					if (writable) {
						_outcome = open_outcome::reset;
						break;
					}

				default:
					throw_error(fpath, errc::dirty, "dirty flag set");
				}
			}
		}
		else if (auto dir = fpath.parent_path(); !dir.empty()) {
			_outcome = open_outcome::created;
			fs::create_directories(dir);
		}

		if (db_file_size < db_min_size)
			db_file_size = db_min_size;

		if (!file_exists || _outcome == open_outcome::reset) {
			touch(fpath);
			fs::resize_file(fpath, db_file_size);

			_file_mapping = bip::file_mapping(fpath.generic_string().c_str(), bip::read_write);
			_file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);

			_segment_manager = new (get_address_at_offset(_file_mapped_region, header_size)) segment_manager(db_file_size - header_size);
			new (_file_mapped_region.get_address()) db_header;
		}
		else if (writable) {
			auto existing_file_size = fs::file_size(fpath);
			size_t grow = 0;

			if (db_file_size > existing_file_size) {
				grow = db_file_size - existing_file_size;
				fs::resize_file(fpath, db_file_size);
			}

			_file_mapping = bip::file_mapping(fpath.generic_string().c_str(), bip::read_write);
			_file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);

			_segment_manager = get_address_at_offset<segment_manager>(_file_mapped_region, header_size);
			if (grow)
				_segment_manager->grow(grow);
		}
		else {
			_file_mapping = bip::file_mapping(fpath.generic_string().c_str(), bip::read_only);
			_file_mapped_region = bip::mapped_region(_file_mapping, bip::read_only);
			_segment_manager = get_address_at_offset<segment_manager>(_file_mapped_region, header_size);
		}

		if (writable) {
			_file_lock = bip::file_lock(fpath.generic_string().c_str());
			if (!_file_lock.try_lock())
				throw_error(fpath, errc::no_access, "could not acquire file lock");

			dirty();
		}
	}

	database::~database() noexcept
	{
		_index_list.clear();
		_index_map.clear();

		if (_segment_manager && _mode == open_mode::read_write)
			flush();
	}

	database::database(database&& other) noexcept
	    : _segment_manager { std::exchange(other._segment_manager, nullptr) }
		, _mode { other._mode }
		, _outcome { other._outcome }
		, _file_path { std::move(_file_path) }
		, _file_lock { std::move(_file_lock) }
		, _file_mapping { std::move(_file_mapping) }
		, _file_mapped_region { std::move(_file_mapped_region) }
		, _index_list { std::move(_index_list) }
		, _index_map { std::move(_index_map) }
	{}

	database& database::operator=(database&& other) noexcept
	{
		if (this == &other)
			return *this;

		if (_segment_manager && _mode == open_mode::read_write)
			flush();

		_segment_manager = std::exchange(other._segment_manager, nullptr);
		_mode = other._mode;
		_outcome = other._outcome;
		_file_path = std::move(_file_path);
		_file_lock = std::move(_file_lock);
		_file_mapping = std::move(_file_mapping);
		_file_mapped_region = std::move(_file_mapped_region);
		_index_list = std::move(_index_list);
		_index_map = std::move(_index_map);

		return *this;
	}

	void database::undo()
	{
		for (auto& item : _index_list) {
			item->undo();
		}
	}

	void database::squash()
	{
		for (auto& item : _index_list) {
			item->squash();
		}
	}

	void database::commit(int64_t revision)
	{
		for (auto& item : _index_list) {
			item->commit(revision);
		}
	}

	void database::undo_all()
	{
		for (auto& item : _index_list) {
			item->undo_all();
		}
	}

	database::session database::start_undo_session()
	{
		std::vector<std::unique_ptr<detail::abstract_undo_session>> _sub_sessions;

		_sub_sessions.reserve(_index_list.size());

		for (auto& item : _index_list) {
			_sub_sessions.push_back(item->start_undo_session());
		}

		return session { std::move(_sub_sessions) };
	}

	void database::flush()
	{
		auto& is_dirty = *get_address_at_offset<uint8_t>(_file_mapped_region, header_dirty_bit_offset);
		if (!is_dirty)
			return;

		is_dirty = false;
		_file_mapped_region.flush(0, 0, false);
	}

	void database::dirty()
	{
		auto& is_dirty = *get_address_at_offset<uint8_t>(_file_mapped_region, header_dirty_bit_offset);
		if (is_dirty)
			return;

		is_dirty = true;
		_file_mapped_region.flush(0, 0, false);
	}

	bool database::dirty() const
	{
		return *get_address_at_offset<uint8_t>(_file_mapped_region, header_dirty_bit_offset);
	}
}
