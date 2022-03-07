#pragma once

#include <chainbase/detail/utility.hpp>
#include <chainbase/detail/binary_iarchive.hpp>
#include <chainbase/detail/binary_oarchive.hpp>
#include <chainbase/database.hpp>

#include <boost/throw_exception.hpp>

#include <fstream>
#include <stdexcept>
#include <filesystem>

namespace chainbase::detail {
	namespace fs = std::filesystem;

	enum class journal_op : uint32_t {
		create = 1,
		insert,
		modify,
		remove,
		start_undo,
		commit,
		undo,
		undo_all,
		revision
	};

	struct journal_log {
		uint32_t crc;
		uint32_t size;
		uint32_t type;
	};

	class journal {
	public:
		journal() = default;

		journal(journal const&) = delete;
		journal& operator=(journal const&) = delete;

		journal(journal&& other)
			: _file { std::move(other._file) }
		{}

		journal& operator=(journal&& other)
		{
			assert(std::addressof(other) != this);
			_file = std::move(other._file);
			return *this;
		}

		template<class Container>
		void open(fs::path const& path, bool recover, Container& c)
		{
			if (!open(path) || !recover)
				return;

			if (!_file.seekg(0)) {
				_file.clear();
				return;
			}

			binary_iarchive input { _file };
			journal_log log {};
			auto pos = _file.tellg();

			while (read_header(log, pos, input)) {
				if (!apply_recover(c, from_underlying<journal_op>(log.type), input))
					input.skip(log.size);
			}

			_file.seekp(pos);
			assert(_file);
		}

		template<class Object>
		void insert(Object const& obj)
		{
			journal_log entry { 0, 0, big_endian_order(to_underlying(journal_op::insert)) };
			auto pos = write_header(entry);
			bs::serialize_adl(_output, const_cast<Object&>(obj), bs::version<Object>::value);
			update_header(entry, pos);
		}

		template<class Object>
		void modify(Object const& obj)
		{
			journal_log entry { 0, 0, big_endian_order(to_underlying(journal_op::modify)) };
			auto pos = write_header(entry);
			_output << obj.id;
			bs::serialize_adl(_output, const_cast<Object&>(obj), bs::version<Object>::value);
			update_header(entry, pos);
		}

		template<class Object>
		void remove(Object const& obj)
		{
			journal_log entry { 0, 0, big_endian_order(to_underlying(journal_op::remove)) };
			auto pos = write_header(entry);
			_output << obj.id;
			update_header(entry, pos);
		}

		void start_undo(int64_t revision) { undo_entry(revision, journal_op::start_undo); }
		void commit(int64_t revision) { undo_entry(revision, journal_op::commit); }
		void undo(int64_t revision) { undo_entry(revision, journal_op::undo); }
		void undo_all(int64_t revision) { undo_entry(revision, journal_op::undo_all); }
		void set_revision(int64_t revision) { undo_entry(revision, journal_op::revision); }

	private:
		template<class Container>
		bool apply_recover(Container& c, journal_op op, binary_iarchive& input)
		{
			using value_type = typename Container::value_type;
			using id_type = typename value_type::id_type;

			switch (op) {
			case journal_op::insert: {
				c.emplace([&input](value_type& obj) { bs::serialize_adl(input, obj, bs::version<value_type>::value); });
				break;
			}

			case detail::journal_op::modify: {
				auto& idx0 = c.template get<0>();
				id_type id;

				input >> id;

				if (auto it = idx0.find(id); it != idx0.end())
					c.modify(*it, [&input](value_type& obj) { bs::serialize_adl(input, obj, bs::version<value_type>::value); });
				else
					BOOST_THROW_EXCEPTION(std::runtime_error("journal recover modify of non existing id " + std::to_string(id)));
				break;
			}

			case detail::journal_op::remove: {
				auto& idx0 = c.template get<0>();
				id_type id;

				input >> id;

				if (auto it = idx0.find(id); it != idx0.end())
					c.remove(*it);
				else
					BOOST_THROW_EXCEPTION(std::runtime_error("journal recover delete of non existing id " + std::to_string(id)));
				break;
			}

			default:
				if constexpr (is_undo_multi_index_v<Container>)
					return apply_recover_undo(c, op, input);

				return false;
			}

			return true;
		}

		template<class Container>
		bool apply_recover_undo(Container& c, journal_op op, binary_iarchive& input)
		{
			switch (op) {
			case journal_op::start_undo: {
				int64_t revision;

				input >> revision;

				if (c.revision() < revision - 1)
					c.set_revision(revision - 1);
				auto rev = c.start_undo();
				if (rev != revision)
					BOOST_THROW_EXCEPTION(std::runtime_error("journal start undo revision missmatch " + std::to_string(rev) + " with expected " + std::to_string(revision)));
				break;
			}

			case journal_op::commit: {
				int64_t revision;

				input >> revision;
				auto rev = c.commit(revision);
				if (rev != revision)
					BOOST_THROW_EXCEPTION(std::runtime_error("journal start undo revision missmatch " + std::to_string(rev) + " with expected " + std::to_string(revision)));
				break;
			}

			case journal_op::undo: {
				int64_t revision;

				input >> revision;
				c.undo();
				if (c.revision() != revision)
					BOOST_THROW_EXCEPTION(std::runtime_error("journal start undo revision missmatch " + std::to_string(c.revision()) + " with expected " + std::to_string(revision)));
				break;
			}

			case journal_op::undo_all: {
				int64_t revision;

				input >> revision;
				c.undo_all();

				if (c.revision() != revision)
					BOOST_THROW_EXCEPTION(std::runtime_error("journal start undo revision missmatch " + std::to_string(c.revision()) + " with expected " + std::to_string(revision)));
				break;
			}

			case journal_op::revision: {
				int64_t revision;

				input >> revision;
				c.set_revision(revision);
				break;
			}

			default:
				return false;
			}

			return true;
		}

		bool open(fs::path const& path);
		void undo_entry(int64_t revision, journal_op op);
		auto write_header(journal_log& log) -> std::fstream::pos_type;
		void update_header(journal_log& log, std::fstream::pos_type old_pos);
		bool read_header(journal_log& log, std::fstream::pos_type& pos, binary_iarchive& input);

	protected:
		std::fstream _file;
		binary_oarchive _output { _file };

		struct entry_t {
			journal_op op;
			std::fstream::pos_type pos;
		} _last_entry {};
	};
}
