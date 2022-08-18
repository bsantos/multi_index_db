#pragma once

#include <chainbase/traits.hpp>
#include <chainbase/detail/journal.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace chainbase {
	/**
	 *  Journaling container extension
	 */
	template<class Container>
	class journaled;

	/**
	 *  Journaling container extension (basic_multi_index specialization)
	 */
	template<class T, class A, class... I>
	class journaled<basic_multi_index<T, A, I...>> {
		journaled(journaled const&) = delete;
		journaled& operator=(journaled const&) = delete;

	public:
		using container_type = basic_multi_index<T, A, I...>;
		using value_type = typename container_type::value_type;
		using id_type = typename container_type::id_type;

	public:
		journaled() = default;
		explicit journaled(container_type* container, fs::path const& path, open_mode mode, open_outcome outcome)
		    : _container { container }
		{
			if (mode == open_mode::read_write)
				_journal.open(path, outcome == open_outcome::reset || outcome == open_outcome::created, container());
		}

		journaled(journaled&& other)
			: _container { std::exchange(other._container, nullptr) }
			, _journal { std::move(other._journal) }
		{}

		journaled& operator=(journaled&& other)
		{
			assert(std::addressof(other) != this);
			_container = std::exchange(other._container, nullptr);
			_journal = std::move(other._journal);
			return *this;
		}

		template<class Constructor>
		const value_type& emplace(Constructor&& c)
		{
			auto& obj = container().emplace(std::forward<Constructor>(c));
			_journal.insert(obj);
			return obj;
		}

		template<class Modifier>
		void modify(const value_type& obj, Modifier&& m)
		{
			container().modify(obj, std::forward<Modifier>(m));
			_journal.modify(obj);
		}

		void remove(const value_type& obj) noexcept
		{
			_journal.remove(obj);
			container().remove(obj);
		}

		explicit operator bool() const { return _container != nullptr; }

		const container_type& indices() const
		{
			return container();
		}

		template<typename CompatibleKey>
		const value_type* find(CompatibleKey&& key) const
		{
			return container().find(std::forward<CompatibleKey>(key));
		}

		template<class Tag>
		const auto& get() const
		{
			return container().template get<Tag>();
		}

		template<int N>
		const auto& get() const
		{
			return container().template get<N>();
		}

		std::size_t size() const
		{
			return container().size();
		}

		bool empty() const
		{
			return container().empty();
		}

		template<class Tag, class Iter>
		auto project(Iter iter) const
		{
			return container().template project<Tag>(iter);
		}

		template<int N, class Iter>
		auto project(Iter iter) const
		{
			return container().template project<N>(iter);
		}

		auto begin() const
		{
			return container().begin();
		}
		auto end() const
		{
			return container().end();
		}

	private:
		container_type& container()
		{
			assert(_container != nullptr);
			return *_container;
		}

		const container_type& container() const
		{
			assert(_container != nullptr);
			return *_container;
		}

	private:
		container_type* _container {};
		detail::journal _journal;
	};

	/**
	 *  Journaling container extension (basic_undo_multi_index specialization)
	 */
	template<class T, class A, class... I>
	class journaled<basic_undo_multi_index<T, A, I...>> {
		journaled(journaled const&) = delete;
		journaled& operator=(journaled const&) = delete;

	public:
		using container_type = basic_undo_multi_index<T, A, I...>;
		using value_type = typename container_type::value_type;
		using id_type = typename container_type::id_type;

	public:
		journaled() = default;
		explicit journaled(container_type* c, fs::path const& path, open_mode mode, open_outcome outcome)
		    : _container { c }
		{
			if (mode == open_mode::read_write)
				_journal.open(path, outcome == open_outcome::reset || outcome == open_outcome::created, container());
		}

		journaled(journaled&& other)
			: _container { std::exchange(other._container, nullptr) }
			, _journal { std::move(other._journal) }
		{}

		journaled& operator=(journaled&& other)
		{
			assert(std::addressof(other) != this);
			_container = std::exchange(other._container, nullptr);
			_journal = std::move(other._journal);
			return *this;
		}

		template<class Constructor>
		const value_type& emplace(Constructor&& c)
		{
			auto& obj = container().emplace(std::forward<Constructor>(c));
			_journal.insert(obj);
			return obj;
		}

		template<class Modifier>
		void modify(const value_type& obj, Modifier&& m)
		{
			container().modify(obj, std::forward<Modifier>(m));
			_journal.modify(obj);
		}

		void remove(const value_type& obj) noexcept
		{
			_journal.remove(obj);
			container().remove(obj);
		}

		explicit operator bool() const { return _container != nullptr; }

		const container_type& indices() const
		{
			return container();
		}

		template<typename CompatibleKey>
		const value_type* find(CompatibleKey&& key) const
		{
			return container().find(std::forward<CompatibleKey>(key));
		}

		template<class Tag>
		const auto& get() const
		{
			return container().template get<Tag>();
		}

		template<int N>
		const auto& get() const
		{
			return container().template get<N>();
		}

		std::size_t size() const
		{
			return container().size();
		}

		bool empty() const
		{
			return container().empty();
		}

		template<class Tag, class Iter>
		auto project(Iter iter) const
		{
			return container().template project<Tag>(iter);
		}

		template<int N, class Iter>
		auto project(Iter iter) const
		{
			return container().template project<N>(iter);
		}

		auto begin() const
		{
			return container().begin();
		}
		auto end() const
		{
			return container().end();
		}

		auto revision() const
		{
			return container().revision();
		}

		auto start_undo_session()
		{
			auto session = container().start_undo_session();
			_journal.start_undo(container().revision());
			return session;
		}

		auto start_undo()
		{
			auto revison = container().start_undo();
			_journal.start_undo(revison);
			return revison;
		}

		void set_revision(uint64_t revision)
		{
			container().set_revision(revision);
			_journal.set_revision(static_cast<int64_t>(revision));
		}

		auto undo_stack_revision_range() const
		{
			return container().undo_stack_revision_range();
		}

		int64_t commit(int64_t revision) noexcept
		{
			revision = container().commit(revision);
			_journal.commit(revision);
			return revision;
		}

		bool has_undo_session() const
		{
			return container().has_undo_session();
		}

		auto last_undo_session() const
		{
			return container().last_undo_session();
		}

		void undo_all()
		{
			if (container().has_undo_session()) {
				container().undo_all();
				_journal.undo_all(container().revision());
			}
		}

		void undo() noexcept
		{
			container().undo();
			_journal.undo(container().revision());
		}

	private:
		container_type& container()
		{
			assert(_container != nullptr);
			return *_container;
		}

		const container_type& container() const
		{
			assert(_container != nullptr);
			return *_container;
		}

	private:
		container_type* _container {};
		detail::journal _journal;
	};
}
