#pragma once

#include <chainbase/multi_index.hpp>

#include <boost/container/deque.hpp>
#include <boost/throw_exception.hpp>
#include <boost/range/iterator_range.hpp>

#include <cassert>
#include <memory>
#include <type_traits>

namespace chainbase::detail {
	struct undo_node_extra {
		uint64_t mtime = 0;
	};
}

namespace chainbase {
	template<class T, class Allocator, class... Indices>
	class basic_undo_multi_index : public basic_multi_index<detail::extra_node_tag<T, detail::undo_node_extra>, Allocator, Indices...> {
		using base = basic_multi_index<detail::extra_node_tag<T, detail::undo_node_extra>, Allocator, Indices...>;
		using index0_type = typename base::index0_type;

		static constexpr int erased_flag = 2; // 0,1,and -1 are used by the tree

		struct old_node
		    : detail::hook<index0_type, Allocator>
		    , detail::value_holder<T> {

			using value_type = T;
			using allocator_type = Allocator;

			template<class... A>
			explicit old_node(const T& t)
			    : detail::value_holder<T> { t }
			{}

			uint64_t _mtime = 0;                           // Backup of the node's _mtime, to be restored on undo
			typename base::alloc_traits::pointer _current; // pointer to the actual node
		};

		using old_alloc_traits = typename std::allocator_traits<Allocator>::template rebind_traits<old_node>;

	protected:
		using node = typename base::node;

	public:
		using id_type = typename base::id_type;
		using value_type  = typename base::value_type;
		using allocator_type = typename base::allocator_type;
		using indices_type = typename base::indices_type;
		using index0_set_type = typename base::index0_set_type;
		using alloc_traits = typename base::alloc_traits;
		using pointer = typename base::pointer;
		using const_iterator = typename base::const_iterator;

	public:
		basic_undo_multi_index() = default;
		explicit basic_undo_multi_index(Allocator const& a)
		    : base { a }
		    , _undo_stack { a }
		    , _old_values_allocator { a }
		{}

		explicit basic_undo_multi_index(basic_undo_multi_index const& other, Allocator const& a)
		    : base { other, a }
		    , _undo_stack { a }
		    , _old_values_allocator { a }
		    , _revision { other._revision }
		    , _monotonic_revision { other._monotonic_revision }
		{}

		~basic_undo_multi_index() noexcept
		{
			dispose_undo();
		}

		// Exception safety: strong
		template<class Constructor>
		const value_type& emplace(Constructor&& c)
		{
			auto& obj = base::emplace(std::forward<Constructor>(c));
			on_create(obj);
			return obj;
		}

		// Exception safety: basic.
		// If the modifier leaves the object in a state that conflicts
		// with another object, it will be reverted.
		template<class Modifier>
		void modify(const value_type& obj, Modifier&& m)
		{
			value_type* backup = on_modify(obj);
			base::modify_with_revert(obj, m, [&](value_type& node_ref) {
				if (backup) {
					node_ref = std::move(*backup);
					assert(backup == &_old_values.front());
					_old_values.pop_front_and_dispose([this](pointer p) { dispose_old(*p); });
					return true;
				}

				return false;
			});
		}

		void remove(const value_type& obj) noexcept
		{
			this->remove_and_dispose_if(obj, [this](value_type& obj) { return on_remove(obj); });
		}

		// Allows testing whether a value has been removed
		//
		// The lifetime of an object removed through a removed_nodes_tracker
		// does not end before the removed_nodes_tracker is destroyed or invalidated.
		//
		// A removed_nodes_tracker is invalidated by the following methods:
		// start_undo_session, commit, squash, and undo.
		class removed_nodes_tracker {
		public:
			explicit removed_nodes_tracker(basic_undo_multi_index& idx)
			    : _self(&idx)
			{}

			~removed_nodes_tracker()
			{
				_removed_values.clear_and_dispose([this](value_type* obj) { _self->dispose_node(*obj); });
			}

			removed_nodes_tracker(const removed_nodes_tracker&) = delete;
			removed_nodes_tracker& operator=(const removed_nodes_tracker&) = delete;

			bool is_removed(const value_type& obj) const
			{
				return basic_undo_multi_index::get_removed_field(obj) == erased_flag;
			}

			// Must be used in place of remove
			void remove(const value_type& obj)
			{
				_self->remove(obj, *this);
			}

		private:
			friend class basic_undo_multi_index;

			void save(value_type& obj)
			{
				basic_undo_multi_index::get_removed_field(obj) = erased_flag;
				_removed_values.push_front(obj);
			}

			basic_undo_multi_index* _self;
			detail::list_base<node, T> _removed_values;
		};

	public:
		auto track_removed()
		{
			return removed_nodes_tracker(*this);
		}

		class session {
		public:
			session(basic_undo_multi_index& idx)
			    : _index { idx }
			{
				idx.start_undo();
			}

			session(session&& other)
			    : _index(other._index)
			    , _apply(other._apply)
			{
				other._apply = false;
			}

			session& operator=(session&& other)
			{
				if (this != &other) {
					undo();
					_apply = other._apply;
					other._apply = false;
				}
				return *this;
			}

			~session()
			{
				if (_apply)
					_index.undo();
			}

			void push()
			{
				_apply = false;
			}

			void squash()
			{
				if (_apply)
					_index.squash();
				_apply = false;
			}

			void undo()
			{
				if (_apply)
					_index.undo();
				_apply = false;
			}

		private:
			basic_undo_multi_index& _index;
			bool _apply = true;
		};

		int64_t revision() const
		{
			return _revision;
		}

		session start_undo_session()
		{
			return session { *this };
		}

		int64_t start_undo()
		{
			_undo_stack.emplace_back();
			_undo_stack.back().old_values_end = _old_values.empty() ? nullptr : &*_old_values.begin();
			_undo_stack.back().removed_values_end = _removed_values.empty() ? nullptr : &*_removed_values.begin();
			_undo_stack.back().old_next_id = this->_next_id;
			_undo_stack.back().ctime = ++_monotonic_revision;
			return ++_revision;
		}

		void set_revision(uint64_t revision)
		{
			if (_undo_stack.size() != 0)
				BOOST_THROW_EXCEPTION(std::logic_error("cannot set revision while there is an existing undo stack"));

			if (revision > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
				BOOST_THROW_EXCEPTION(std::logic_error("revision to set is too high"));

			if (static_cast<int64_t>(revision) < _revision)
				BOOST_THROW_EXCEPTION(std::logic_error("revision cannot decrease"));

			_revision = static_cast<int64_t>(revision);
		}

		std::pair<int64_t, int64_t> undo_stack_revision_range() const
		{
			return { _revision - _undo_stack.size(), _revision };
		}

		/**
		 * Discards all undo history prior to revision
		 */
		int64_t commit(int64_t revision) noexcept
		{
			revision = std::min(revision, _revision);
			if (revision == _revision) {
				dispose_undo();
				_undo_stack.clear();
			}
			else if (static_cast<uint64_t>(_revision - revision) < _undo_stack.size()) {
				auto iter = _undo_stack.begin() + (_undo_stack.size() - (_revision - revision));
				dispose(get_old_values_end(*iter), get_removed_values_end(*iter));
				_undo_stack.erase(_undo_stack.begin(), iter);
			}

			return _revision;
		}

		bool has_undo_session() const
		{
			return !_undo_stack.empty();
		}

		struct delta {
			boost::iterator_range<typename index0_set_type::const_iterator> new_values;
			boost::iterator_range<typename detail::list_base<old_node, index0_type>::const_iterator> old_values;
			boost::iterator_range<typename detail::list_base<node, index0_type>::const_iterator> removed_values;
		};

		delta last_undo_session() const
		{
			if (_undo_stack.empty())
				return { { this->template get<0>().end(), this->template get<0>().end() }, { _old_values.end(), _old_values.end() }, { _removed_values.end(), _removed_values.end() } };
			// Warning: This is safe ONLY as long as nothing exposes the undo stack to client code.
			// Compressing the undo stack does not change the logical state of the basic_multi_index.
			const_cast<basic_undo_multi_index*>(this)->compress_last_undo_session();
			return { { this->template get<0>().lower_bound(_undo_stack.back().old_next_id), this->template get<0>().end() },
				     { _old_values.begin(), get_old_values_end(_undo_stack.back()) },
				     { _removed_values.begin(), get_removed_values_end(_undo_stack.back()) } };
		}

		void undo_all()
		{
			while (!_undo_stack.empty()) {
				undo();
			}
		}

		// Resets the contents to the state at the top of the undo stack.
		void undo() noexcept
		{
			if (_undo_stack.empty())
				return;
			undo_state& undo_info = _undo_stack.back();
			// erase all new_ids
			this->erase_new_ids(undo_info.old_next_id);
			// replace old_values
			_old_values.erase_after_and_dispose(_old_values.before_begin(), get_old_values_end(undo_info), [this, &undo_info](pointer p) {
				auto restored_mtime = to_old_node(*p)._mtime;
				// Skip restoring values that overwrite an earlier modify in the same session.
				// Duplicate modifies can only happen because of squash.
				if (restored_mtime < undo_info.ctime) {
					auto iter = &to_old_node(*p)._current->_item;
					*iter = std::move(*p);
					auto& node_mtime = to_node(*iter)._extra.mtime;
					node_mtime = restored_mtime;
					if (get_removed_field(*iter) != erased_flag) {
						// Non-unique items are transient and are guaranteed to be fixed
						// by the time we finish processing old_values.
						this->post_modify_transient(*iter);
					}
					else {
						// The item was removed.  It will be inserted when we process removed_values
					}
				}
				dispose_old(*p);
			});
			// insert all removed_values
			_removed_values.erase_after_and_dispose(_removed_values.before_begin(), get_removed_values_end(undo_info), [this, &undo_info](pointer p) {
				if (p->id < undo_info.old_next_id) {
					get_removed_field(*p) = 0; // Will be overwritten by tree algorithms, because we're reusing the color.
					this->insert(*p);
				}
				else {
					this->dispose_node(*p);
				}
			});
			this->_next_id = undo_info.old_next_id;
			_undo_stack.pop_back();
			--_revision;
		}

		// Combines the top two states on the undo stack
		void squash() noexcept
		{
			squash_and_compress();
		}

		void squash_fast() noexcept
		{
			if (_undo_stack.empty()) {
				return;
			}
			else if (_undo_stack.size() == 1) {
				dispose_undo();
			}
			_undo_stack.pop_back();
			--_revision;
		}

		void squash_and_compress() noexcept
		{
			if (_undo_stack.size() >= 2) {
				compress_impl(_undo_stack[_undo_stack.size() - 2]);
			}
			squash_fast();
		}

		void compress_last_undo_session() noexcept
		{
			compress_impl(_undo_stack.back());
		}

	private:
		// The undo stack is implemented as a deque of undo_states
		// that index into a pair of singly linked lists.
		//
		// The primary key (id) is managed by the basic_multi_index.  The id's are assigned sequentially to
		// objects in the order of insertion.  User code MUST NOT modify the primary key.
		// A given id can only be reused if its insertion is undone.
		//
		// Each undo session remembers the state of the table at the point when it was created.
		//
		// Within the undo state at the top of the undo stack:
		// A primary key is new if it is at least old_next_id.
		//
		// A primary key is removed if it exists in the removed_values list before removed_values_end.
		// A node has a flag which indicates whether it has been removed.
		//
		// A primary key is modified if it exists in the old_values list before old_values_end
		//
		// A primary key exists at most once in either the main table or removed values.
		// Every primary key in old_values also exists in either the main table OR removed_values.
		// If a primary key exists in both removed_values AND old_values, undo will restore the value from old_values.
		// A primary key may appear in old_values any number of times.  If it appears more than once
		//   within a single undo session, undo will restore the oldest value.
		//
		// The following describes the minimal set of operations required to maintain the undo stack:
		// start session: remember next_id and the current heads of old_values and removed_values.
		// squash: drop the last undo state
		// create: nop
		// modify: push a copy of the object onto old_values
		// remove: move node to removed index and set removed flag
		//
		// Operations on a given key MUST always follow the sequence: CREATE MODIFY* REMOVE?
		// When undoing multiple operations on the same key, the final result is determined
		// by the oldest operation.  Therefore, the following optimizations can be applied:
		//  - A primary key which is new may be discarded from old_values and removed_values
		//  - If a primary key has multiple modifications, all but the oldest can be discarded.
		//  - If a primary key is both modified and removed, the modified value can replace
		//    the removed value, and can then be discarded.
		// These optimizations may be applied at any time, but are not required by the class
		// invariants.
		//
		// Notes regarding memory:
		// Nodes in the main table share the same layout as nodes in removed_values and may
		// be freely moved between the two.  This permits undo to restore removed nodes
		// without allocating memory.
		//
		struct undo_state {
			typename std::allocator_traits<Allocator>::pointer old_values_end;
			typename std::allocator_traits<Allocator>::pointer removed_values_end;
			id_type old_next_id {};
			uint64_t ctime = 0; // _monotonic_revision at the point the undo_state was created
		};

		void on_create(const value_type& value) noexcept
		{
			if (!_undo_stack.empty()) {
				// Not in old_values, removed_values, or new_ids
				to_node(value)._extra.mtime = _monotonic_revision;
			}
		}

		value_type* on_modify(const value_type& obj)
		{
			if (!_undo_stack.empty()) {
				auto& undo_info = _undo_stack.back();
				if (to_node(obj)._extra.mtime >= undo_info.ctime) {
					// Nothing to do
				}
				else {
					// Not in removed_values
					auto p = old_alloc_traits::allocate(_old_values_allocator, 1);
					auto guard0 = detail::scope_exit { [&] { _old_values_allocator.deallocate(p, 1); } };
					old_alloc_traits::construct(_old_values_allocator, &*p, obj);
					p->_mtime = to_node(obj)._extra.mtime;
					p->_current = &to_node(obj);
					guard0.cancel();
					_old_values.push_front(p->_item);
					to_node(obj)._extra.mtime = _monotonic_revision;
					return &p->_item;
				}
			}

			return nullptr;
		}

		// returns true if the node should be destroyed
		bool on_remove(value_type& obj)
		{
			if (!_undo_stack.empty()) {
				auto& undo_info = _undo_stack.back();
				if (obj.id >= undo_info.old_next_id) {
					return true;
				}
				get_removed_field(obj) = erased_flag;

				_removed_values.push_front(obj);
				return false;
			}

			return true;
		}

		void remove(const value_type& obj, removed_nodes_tracker& tracker) noexcept
		{
			auto& node_ref = const_cast<value_type&>(obj);
			erase(node_ref);
			if (on_remove(node_ref)) {
				tracker.save(node_ref);
			}
		}

		// Removes elements of the last undo session that would be redundant
		// if all the sessions after @c session were squashed.
		//
		// WARNING: This function leaves any undo sessions after @c session in
		// an indeterminate state.  The caller MUST use squash to restore the
		// undo stack to a sane state.
		void compress_impl(undo_state& session) noexcept
		{
			auto session_start = session.ctime;
			auto old_next_id = session.old_next_id;
			detail::remove_if_after_and_dispose(
			    _old_values, _old_values.before_begin(), get_old_values_end(_undo_stack.back()),
			    [session_start](value_type& v) {
				    if (to_old_node(v)._mtime >= session_start)
					    return true;
				    auto& item = to_old_node(v)._current->_item;
				    if (get_removed_field(item) == erased_flag) {
					    item = std::move(v);
					    to_node(item)._extra.mtime = to_old_node(v)._mtime;
					    return true;
				    }
				    return false;
			    },
			    [&](pointer p) { dispose_old(*p); });
			detail::remove_if_after_and_dispose(
			    _removed_values, _removed_values.before_begin(), get_removed_values_end(_undo_stack.back()),
			    [old_next_id](value_type& v) { return v.id >= old_next_id; }, [this](pointer p) { this->dispose_node(*p); });
		}

		void dispose_old(old_node& node_ref) noexcept
		{
			old_node* p { &node_ref };
			old_alloc_traits::destroy(_old_values_allocator, p);
			old_alloc_traits::deallocate(_old_values_allocator, p, 1);
		}

		void dispose_old(value_type& node_ref) noexcept
		{
			dispose_old(static_cast<old_node&>(*boost::intrusive::get_parent_from_member(&node_ref, &detail::value_holder<value_type>::_item)));
		}

		void dispose(typename detail::list_base<old_node, index0_type>::iterator old_start,
		             typename detail::list_base<node, index0_type>::iterator removed_start) noexcept
		{
			// This will leave one element around.  That's okay, because we'll clean it up the next time.
			if (old_start != _old_values.end())
				_old_values.erase_after_and_dispose(old_start, _old_values.end(), [this](pointer p) { dispose_old(*p); });
			if (removed_start != _removed_values.end())
				_removed_values.erase_after_and_dispose(removed_start, _removed_values.end(), [this](pointer p) { this->dispose_node(*p); });
		}

		void dispose_undo() noexcept
		{
			_old_values.clear_and_dispose([this](pointer p) { dispose_old(*p); });
			_removed_values.clear_and_dispose([this](pointer p) { this->dispose_node(*p); });
		}

		static node& to_node(value_type& obj)
		{
			return base::to_node(obj);
		}

		static node& to_node(const value_type& obj)
		{
			return base::to_node(obj);
		}

		static old_node& to_old_node(value_type& obj)
		{
			return static_cast<old_node&>(*boost::intrusive::get_parent_from_member(&obj, &detail::value_holder<value_type>::_item));
		}

		auto get_old_values_end(const undo_state& info)
		{
			if (info.old_values_end == nullptr) {
				return _old_values.end();
			}
			else {
				return _old_values.iterator_to(*info.old_values_end);
			}
		}

		auto get_old_values_end(const undo_state& info) const
		{
			return static_cast<decltype(_old_values.cend())>(const_cast<basic_undo_multi_index*>(this)->get_old_values_end(info));
		}

		auto get_removed_values_end(const undo_state& info)
		{
			if (info.removed_values_end == nullptr) {
				return _removed_values.end();
			}
			else {
				return _removed_values.iterator_to(*info.removed_values_end);
			}
		}

		auto get_removed_values_end(const undo_state& info) const
		{
			return static_cast<decltype(_removed_values.cend())>(const_cast<basic_undo_multi_index*>(this)->get_removed_values_end(info));
		}

		// Returns the field indicating whether the node has been removed
		static int& get_removed_field(const value_type& obj)
		{
			return static_cast<detail::hook<index0_type, Allocator>&>(to_node(obj))._color;
		}

	private:
		boost::container::deque<undo_state, detail::rebind_alloc_t<Allocator, undo_state>> _undo_stack;
		detail::list_base<old_node, index0_type> _old_values;
		detail::list_base<node, index0_type> _removed_values;
		detail::rebind_alloc_t<Allocator, old_node> _old_values_allocator;
		int64_t _revision = 0;
		uint64_t _monotonic_revision = 0;
	};

	template<class Object, class... Indices>
	using undo_multi_index = basic_undo_multi_index<Object, allocator<Object>, Indices...>;
}
