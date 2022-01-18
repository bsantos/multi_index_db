#include <chainbase/chainbase.hpp>

namespace chainbase {
	database::database(const fs::path& dir, open_flags flags, uint64_t shared_file_size, bool allow_dirty)
		: _db_file(dir, flags & database::read_write, shared_file_size, allow_dirty)
		, _read_only(flags == database::read_only)
	{}

	database::~database()
	{
		_index_list.clear();
		_index_map.clear();
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
}
