#pragma once

#include <memory>

namespace chainbase {
	template<class T, class A, class... I>
	class basic_multi_index;
}

namespace chainbase::detail {
	template<class MultiIndexType>
	struct get_multi_index_impl;

	template<class T, class A, class... I>
	struct get_multi_index_impl<basic_multi_index<T, A, I...>> {
		using type = basic_multi_index<T, A, I...>;
	};

	template<class MultiIndexType>
	using get_multi_index = typename get_multi_index_impl<MultiIndexType>::type;

	class abstract_undo_session {
	public:
		virtual ~abstract_undo_session() {};
		virtual void push() = 0;
		virtual void squash() = 0;
		virtual void undo() = 0;
	};

	template<class SessionType>
	class undo_session_impl : public abstract_undo_session {
	public:
		undo_session_impl(SessionType&& s)
		    : _session(std::move(s))
		{}

		virtual void push() override
		{
			_session.push();
		}

		virtual void squash() override
		{
			_session.squash();
		}

		virtual void undo() override
		{
			_session.undo();
		}

	private:
		SessionType _session;
	};

	class abstract_multi_index {
	public:
		abstract_multi_index(void* i)
		    : _idx_ptr(i)
		{}
		virtual ~abstract_multi_index() {}

		virtual std::unique_ptr<abstract_undo_session> start_undo_session() = 0;

		virtual void set_revision(uint64_t revision) = 0;
		virtual int64_t revision() const = 0;
		virtual void undo() const = 0;
		virtual void squash() const = 0;
		virtual void commit(int64_t revision) const = 0;
		virtual void undo_all() const = 0;
		virtual std::pair<int64_t, int64_t> undo_stack_revision_range() const = 0;

		void* get() const
		{
			return _idx_ptr;
		}

	private:
		void* _idx_ptr;
	};

	template<typename IndexType>
	class multi_index : public abstract_multi_index {
	public:
		multi_index(IndexType& idx)
		    : abstract_multi_index { &idx }
		{}

		std::unique_ptr<abstract_undo_session> start_undo_session() override
		{
			return std::unique_ptr<abstract_undo_session>(new undo_session_impl<typename IndexType::session>(get().start_undo_session()));
		}

		void set_revision(uint64_t revision) override
		{
			get().set_revision(revision);
		}
		int64_t revision() const override
		{
			return get().revision();
		}
		void undo() const override
		{
			get().undo();
		}
		void squash() const override
		{
			get().squash();
		}
		void commit(int64_t revision) const override
		{
			get().commit(revision);
		}
		void undo_all() const override
		{
			get().undo_all();
		}
		std::pair<int64_t, int64_t> undo_stack_revision_range() const override
		{
			return get().undo_stack_revision_range();
		}

		IndexType& get() const
		{
			return *static_cast<IndexType*>(abstract_multi_index::get());
		}
	};
}
