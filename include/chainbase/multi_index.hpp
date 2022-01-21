#pragma once

#include <chainbase/detail/allocator.hpp>
#include <chainbase/detail/scope_exit.hpp>
#include <chainbase/detail/multi_index.hpp>
#include <chainbase/allocator.hpp>

#include <boost/throw_exception.hpp>

#include <cassert>
#include <memory>
#include <type_traits>

namespace chainbase {
	/**
	 *  A container version of Boost.MultiIndex for memory mapped databases
	 */
	template<class T, class Allocator, class... Indices>
	class basic_multi_index {
	protected:
		using extra_traits = detail::extra_node_traits<T>;
		using extra_extra_traits = detail::extra_node_traits<typename extra_traits::value_type>;

		static_assert(std::is_same_v<typename extra_extra_traits::extra_type, void>, "Only one derived class can use extra_node_tag");

		struct node
		    : detail::hook<Indices, Allocator>...
		    , detail::value_holder<typename extra_traits::value_type>
		    , detail::extra_holder<typename extra_traits::extra_type> {

			using value_type = typename extra_traits::value_type;
			using allocator_type = Allocator;

			template<class... A>
			explicit node(A&&... a)
			    : detail::value_holder<value_type> { static_cast<A&&>(a)... }
			{}
		};

	public:
		using value_type = typename extra_traits::value_type;
		using allocator_type = Allocator;
		using id_type = std::decay_t<decltype(std::declval<value_type>().id)>;

		static_assert((... && detail::is_valid_index<Indices>), "Only ordered_unique indices are supported");

		template<class Node, class OrderedIndex>
		struct set_index : private detail::set_base<Node, OrderedIndex> {
			using base_type = detail::set_base<Node, OrderedIndex>;
			// Allow compatible keys to match multi_index
			template<class K>
			auto find(K&& k) const
			{
				return base_type::find(static_cast<K&&>(k), this->key_comp());
			}

			template<class K>
			auto lower_bound(K&& k) const
			{
				return base_type::lower_bound(static_cast<K&&>(k), this->key_comp());
			}

			template<class K>
			auto upper_bound(K&& k) const
			{
				return base_type::upper_bound(static_cast<K&&>(k), this->key_comp());
			}

			template<class K>
			auto equal_range(K&& k) const
			{
				return base_type::equal_range(static_cast<K&&>(k), this->key_comp());
			}

			using base_type::begin;
			using base_type::empty;
			using base_type::end;
			using base_type::iterator_to;
			using base_type::rbegin;
			using base_type::rend;
			using base_type::size;

			friend class basic_multi_index;
		};

		using indices_type = std::tuple<set_index<node, Indices>...>;
		using index0_type = boost::mp11::mp_first<boost::mp11::mp_list<Indices...>>;
		using index0_set_type = std::tuple_element_t<0, indices_type>;
		using alloc_traits = typename std::allocator_traits<Allocator>::template rebind_traits<node>;

		static_assert(std::is_same_v<typename index0_set_type::key_type, id_type>, "first index must be id");

		using pointer = value_type*;
		using const_iterator = typename index0_set_type::const_iterator;

	public:
		basic_multi_index() = default;
		explicit basic_multi_index(Allocator const& a)
		    : _allocator { a }
		{}

		explicit basic_multi_index(basic_multi_index const& other, Allocator const& a)
	    	: _allocator { a }
		    , _next_id { other._next_id }
		{
			auto& idx0 = std::get<0>(_indices);
			auto guard = detail::scope_exit { [&] { idx0.clear_and_dispose([&](pointer p) { dispose_node(*p); }); } };

			for (auto const& node : std::get<0>(other._indices)) {
				auto p = alloc_traits::allocate(_allocator, 1);
				auto guard = detail::scope_exit { [&] { alloc_traits::deallocate(_allocator, p, 1); } };

				alloc_traits::construct(_allocator, &*p, [&](value_type& v) { v = node; }, detail::propagate_allocator(_allocator));
				idx0.push_back(p->_item);
				insert_impl_nc<1>(p->_item);
				guard.cancel();
			}

			guard.cancel();
		}

		~basic_multi_index() noexcept
		{
			clear_impl<1>();
			std::get<0>(_indices).clear_and_dispose([&](pointer p) { dispose_node(*p); });
		}

		// Exception safety: strong
		template<class Constructor>
		const value_type& emplace(Constructor&& c)
		{
			auto p = alloc_traits::allocate(_allocator, 1);
			auto guard0 = detail::scope_exit { [&] { alloc_traits::deallocate(_allocator, p, 1); } };
			auto new_id = _next_id;
			auto constructor = [&](value_type& v) {
				v.id = new_id;
				c(v);
			};
			alloc_traits::construct(_allocator, &*p, constructor, detail::propagate_allocator(_allocator));
			auto guard1 = detail::scope_exit { [&] { alloc_traits::destroy(_allocator, &*p); } };
			if (!insert_impl<1>(p->_item))
				BOOST_THROW_EXCEPTION(std::logic_error { "could not insert object, most likely a uniqueness constraint was violated" });
			std::get<0>(_indices).push_back(p->_item); // cannot fail and we know that it will definitely insert at the end.
			++_next_id;
			guard1.cancel();
			guard0.cancel();
			return p->_item;
		}

		// Exception safety: basic.
		// If the modifier leaves the object in a state that conflicts
		// with another object, it will be erased.
		template<class Modifier>
		void modify(const value_type& obj, Modifier&& m)
		{
			modify_with_revert(obj, m, [this](value_type const& obj) { return false; });
		}

		void remove(const value_type& obj) noexcept
		{
			remove_and_dispose_if(obj, [](value_type&) { return true; });
		}

	public:
		template<typename CompatibleKey>
		const value_type* find(CompatibleKey&& key) const
		{
			const auto& index = std::get<0>(_indices);
			auto iter = index.find(static_cast<CompatibleKey&&>(key));
			if (iter != index.end()) {
				return &*iter;
			}
			else {
				return nullptr;
			}
		}

		const basic_multi_index& indices() const
		{
			return *this;
		}

		template<class Tag>
		const auto& get() const
		{
			return std::get<detail::find_tag<Tag, Indices...>::value>(_indices);
		}

		template<int N>
		const auto& get() const
		{
			return std::get<N>(_indices);
		}

		std::size_t size() const
		{
			return std::get<0>(_indices).size();
		}

		bool empty() const
		{
			return std::get<0>(_indices).empty();
		}

		template<class Tag, class Iter>
		auto project(Iter iter) const
		{
			return project<detail::find_tag<Tag, Indices...>::value>(iter);
		}

		template<int N, class Iter>
		auto project(Iter iter) const
		{
			if (iter == get<boost::mp11::mp_find<boost::mp11::mp_list<typename set_index<node, Indices>::const_iterator...>, Iter>::value>().end())
				return get<N>().end();
			return get<N>().iterator_to(*iter);
		}

		auto begin() const
		{
			return get<0>().begin();
		}
		auto end() const
		{
			return get<0>().end();
		}

	protected:
		template<class Modifier, class Reverter>
		void modify_with_revert(value_type const& obj, Modifier&& m, Reverter&& revert)
		{
			value_type& node_ref = const_cast<value_type&>(obj);
			bool success = false;
			{
				auto old_id = obj.id;
				auto guard = detail::scope_exit { [&] {
					// The object id cannot be modified
					if (old_id != obj.id) {
						if (!revert(node_ref))
							remove(obj);
					}
					else if (!post_modify_impl<true, 1>(node_ref)) {
						if (revert(node_ref)) {
							bool post_modify_must_succeed [[maybe_unused]] = post_modify_impl<true, 1>(node_ref);
							assert(post_modify_must_succeed);
						}
						else {
							remove(obj);
						}
					}
					else {
						success = true;
					}
				} };
				m(node_ref);
				assert(obj.id == old_id);
			}

			if (!success)
				BOOST_THROW_EXCEPTION(std::logic_error { "could not modify object, most likely a uniqueness constraint was violated" });
		}

		template<class Predicate>
		void remove_and_dispose_if(value_type const& obj, Predicate&& p)
		{
			auto& node_ref = const_cast<value_type&>(obj);
			erase_impl(node_ref);

			if (p(node_ref))
				dispose_node(node_ref);
		}

		void insert(value_type& p)
		{
			bool insert_must_succeed [[maybe_unused]] = insert_impl(p);
			assert(insert_must_succeed);
		}

		void post_modify_transient(value_type& p)
		{
			bool post_modify_transient_must_succeed [[maybe_unused]] = post_modify_impl<false, 1>(p);
			assert(post_modify_transient_must_succeed);
		}

		void erase(value_type& p)
		{
			erase_impl(p);
		}

		void erase_new_ids(id_type id)
		{
			auto& by_id = std::get<0>(_indices);
			auto new_ids_iter = by_id.lower_bound(id);

			by_id.erase_and_dispose(new_ids_iter, by_id.end(), [this](pointer p) {
				erase_impl<1>(*p);
				dispose_node(*p);
			});
		}

		void dispose_node(node& node_ref) noexcept
		{
			node* p { &node_ref };
			alloc_traits::destroy(_allocator, p);
			alloc_traits::deallocate(_allocator, p, 1);
		}

		void dispose_node(value_type& node_ref) noexcept
		{
			dispose_node(static_cast<node&>(*boost::intrusive::get_parent_from_member(&node_ref, &detail::value_holder<value_type>::_item)));
		}

		static node& to_node(value_type& obj)
		{
			return static_cast<node&>(*boost::intrusive::get_parent_from_member(&obj, &detail::value_holder<value_type>::_item));
		}

		static node& to_node(const value_type& obj)
		{
			return to_node(const_cast<value_type&>(obj));
		}

	private:
		template<int N = 0>
		bool insert_impl(value_type& p)
		{
			if constexpr (N < sizeof...(Indices)) {
				auto [iter, inserted] = std::get<N>(_indices).insert_unique(p);
				if (!inserted)
					return false;
				auto guard = detail::scope_exit { [this, iter = iter] { std::get<N>(_indices).erase(iter); } };
				if (insert_impl<N + 1>(p)) {
					guard.cancel();
					return true;
				}
				return false;
			}
			return true;
		}

		template<int N>
		void insert_impl_nc(value_type& p)
		{
			if constexpr (N < sizeof...(Indices)) {
				std::get<N>(_indices).insert_unique(p);
				insert_impl_nc<N + 1>(p);
			}
		}

		// Moves a modified node into the correct location
		template<bool unique, int N = 0>
		bool post_modify_impl(value_type& p)
		{
			if constexpr (N < sizeof...(Indices)) {
				auto& idx = std::get<N>(_indices);
				auto iter = idx.iterator_to(p);
				bool fixup = false;
				if (iter != idx.begin()) {
					auto copy = iter;
					--copy;
					if (!idx.value_comp()(*copy, p))
						fixup = true;
				}
				++iter;
				if (iter != idx.end()) {
					if (!idx.value_comp()(p, *iter))
						fixup = true;
				}
				if (fixup) {
					auto iter2 = idx.iterator_to(p);
					idx.erase(iter2);
					if constexpr (unique) {
						auto [new_pos, inserted] = idx.insert_unique(p);
						if (!inserted) {
							idx.insert_before(new_pos, p);
							return false;
						}
					}
					else {
						idx.insert_equal(p);
					}
				}
				return post_modify_impl<unique, N + 1>(p);
			}
			return true;
		}

		template<int N = 0>
		void erase_impl(value_type& p)
		{
			if constexpr (N < sizeof...(Indices)) {
				auto& setN = std::get<N>(_indices);
				setN.erase(setN.iterator_to(p));
				erase_impl<N + 1>(p);
			}
		}

		template<int N = 0>
		void clear_impl() noexcept
		{
			if constexpr (N < sizeof...(Indices)) {
				std::get<N>(_indices).clear();
				clear_impl<N + 1>();
			}
		}

	private:
		indices_type _indices;
		detail::rebind_alloc_t<Allocator, node> _allocator;

	protected:
		id_type _next_id {};
	};

	template<class Object, class... Indices>
	using multi_index = basic_multi_index<Object, allocator<Object>, Indices...>;
}
