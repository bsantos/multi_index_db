#include <chainbase/detail/journal.hpp>
#include <chainbase/detail/scope_exit.hpp>

namespace chainbase::detail {
	namespace {
		bool can_compress_undo_entry(journal_op last_op, journal_op next_op)
		{
			return (last_op == journal_op::start_undo && next_op == journal_op::commit) ||
			       (last_op == journal_op::revision && (next_op == journal_op::start_undo || next_op == journal_op::revision));
		}
	}

	bool journal::open(fs::path const& path)
	{
		_file.open(path, std::fstream::out | std::fstream::in | std::fstream::binary | std::fstream::ate);
		if (_file)
			return true;

		_file.open(path, std::fstream::out | std::fstream::binary | std::fstream::ate);
		assert(_file);
		if (!_file)
			BOOST_THROW_EXCEPTION(std::runtime_error("failed to open journal file " + path.string() + " for writting"));

		return false;
	}

	void journal::undo_entry(int64_t revision, journal_op op)
	{
		journal_log entry { 0, 0, big_endian_order(to_underlying(op)) };

		if (can_compress_undo_entry(_last_entry.op, op)) {
			if (op != journal_op::start_undo)
				entry.type = big_endian_order(to_underlying(journal_op::revision));

			_file.seekp(_last_entry.pos);
		}

		auto pos = write_header(entry);
		_output << revision;
		update_header(entry, pos);
	}

	auto journal::write_header(journal_log& log) -> std::fstream::pos_type
	{
		auto pos = _file.tellp();
		_file.write(reinterpret_cast<char const*>(&log), sizeof(log));
		return pos;
	}

	void journal::update_header(journal_log& log, std::fstream::pos_type hdr_pos)
	{
		auto pos = _file.tellp();

		_last_entry.op = from_underlying<journal_op>(big_endian_order(log.type));
		_last_entry.pos = hdr_pos;

		log.crc = big_endian_order(_output.checksum());
		log.size = big_endian_order(static_cast<uint32_t>(pos - hdr_pos - sizeof(journal_log)));


		_file.seekp(hdr_pos);
		_file.write(reinterpret_cast<char const*>(&log), sizeof(log));
		_file.seekp(pos);
		std::flush(_file);
	}

	bool journal::read_header(journal_log& log, std::fstream::pos_type& pos, binary_iarchive& input)
	{
		if (auto size = _file.tellg() - pos; size != log.size) {
			pos -= sizeof(log);
			return false;
		}

		if (auto crc = input.checksum(); crc != log.crc) {
			pos -= sizeof(log);
			return false;
		}

		_file.read(reinterpret_cast<char*>(&log), sizeof(log));
		if (!_file) {
			_file.clear();
			pos = _file.tellg();
			return false;
		}

		log.crc = big_endian_order(log.crc);
		log.size = big_endian_order(log.size);
		log.type = big_endian_order(log.type);

		pos = _file.tellg();
		return true;
	}
}
