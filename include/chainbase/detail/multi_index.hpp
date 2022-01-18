#pragma once

#include <utility>
#include <type_traits>

#include <boost/mp11/list.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/avltree.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/multi_index_container_fwd.hpp>

namespace chainbase {
	template<typename T, typename S>
	class chainbase_node_allocator;
}

namespace chainbase::detail {
	template<class KeyExtractor, class T>
	struct get_key {
		using type = std::decay_t<decltype(KeyExtractor {}(std::declval<const T&>()))>;

		decltype(auto) operator()(const T& arg) const { return KeyExtractor {}(arg); }
	};

	template<class T>
	struct value_holder {
		template<class... A>
		value_holder(A&&... a)
		    : _item(static_cast<A&&>(a)...)
		{}

		T _item;
	};

	template<class Tag>
	struct offset_node_base {
		offset_node_base() = default;

		constexpr offset_node_base(const offset_node_base&) {}
		constexpr offset_node_base& operator=(const offset_node_base&) { return *this; }

		std::ptrdiff_t _parent;
		std::ptrdiff_t _left;
		std::ptrdiff_t _right;
		int _color;
	};

	template<class Tag>
	struct offset_node_traits {
		using node = offset_node_base<Tag>;
		using node_ptr = node*;
		using const_node_ptr = const node*;
		using color = int;

		static node_ptr get_parent(const_node_ptr n)
		{
			if (n->_parent == 1)
				return nullptr;
			return (node_ptr) ((char*) n + n->_parent);
		}

		static void set_parent(node_ptr n, node_ptr parent)
		{
			if (parent == nullptr)
				n->_parent = 1;
			else
				n->_parent = (char*) parent - (char*) n;
		}

		static node_ptr get_left(const_node_ptr n)
		{
			if (n->_left == 1)
				return nullptr;
			return (node_ptr) ((char*) n + n->_left);
		}

		static void set_left(node_ptr n, node_ptr left)
		{
			if (left == nullptr)
				n->_left = 1;
			else
				n->_left = (char*) left - (char*) n;
		}

		static node_ptr get_right(const_node_ptr n)
		{
			if (n->_right == 1)
				return nullptr;
			return (node_ptr) ((char*) n + n->_right);
		}

		static void set_right(node_ptr n, node_ptr right)
		{
			if (right == nullptr)
				n->_right = 1;
			else
				n->_right = (char*) right - (char*) n;
		}

		// red-black tree
		static color get_color(node_ptr n)
		{
			return n->_color;
		}

		static void set_color(node_ptr n, color c)
		{
			n->_color = c;
		}

		static color black()
		{
			return 0;
		}

		static color red()
		{
			return 1;
		}

		// avl tree
		using balance = int;

		static balance get_balance(node_ptr n)
		{
			return n->_color;
		}

		static void set_balance(node_ptr n, balance c)
		{
			n->_color = c;
		}

		static balance negative()
		{
			return -1;
		}

		static balance zero()
		{
			return 0;
		}

		static balance positive()
		{
			return 1;
		}

		// list
		static node_ptr get_next(const_node_ptr n)
		{
			return get_right(n);
		}

		static void set_next(node_ptr n, node_ptr next)
		{
			set_right(n, next);
		}

		static node_ptr get_previous(const_node_ptr n)
		{
			return get_left(n);
		}

		static void set_previous(node_ptr n, node_ptr previous)
		{
			set_left(n, previous);
		}
	};

	template<class Node, class Tag>
	struct offset_node_value_traits {
		using node_traits = offset_node_traits<Tag>;
		using node_ptr = typename node_traits::node_ptr;
		using const_node_ptr = typename node_traits::const_node_ptr;
		using value_type = typename Node::value_type;
		using pointer = value_type*;
		using const_pointer = const value_type*;

		static node_ptr to_node_ptr(value_type& value)
		{
			return node_ptr { static_cast<Node*>(boost::intrusive::get_parent_from_member(&value, &value_holder<value_type>::_item)) };
		}
		static const_node_ptr to_node_ptr(const value_type& value)
		{
			return const_node_ptr { static_cast<const Node*>(boost::intrusive::get_parent_from_member(&value, &value_holder<value_type>::_item)) };
		}
		static pointer to_value_ptr(node_ptr n)
		{
			return pointer { &static_cast<Node*>(&*n)->_item };
		}
		static const_pointer to_value_ptr(const_node_ptr n)
		{
			return const_pointer { &static_cast<const Node*>(&*n)->_item };
		}

		static constexpr boost::intrusive::link_mode_type link_mode = boost::intrusive::normal_link;
	};

	template<class Allocator, class T>
	using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

	template<class Index>
	struct index_tag_impl { using type = void; };

	template<template<class...> class Index, class Tag, class... T>
	struct index_tag_impl<Index<boost::multi_index::tag<Tag>, T...>> { using type = Tag; };

	template<class Index>
	using index_tag = typename index_tag_impl<Index>::type;

	template<class Tag, class... Indices>
	using find_tag = boost::mp11::mp_find<boost::mp11::mp_list<index_tag<Indices>...>, Tag>;

	template<class K, class Allocator>
	using hook = offset_node_base<K>;

	template<class Node, class OrderedIndex>
	using set_base = boost::intrusive::avltree<typename Node::value_type, boost::intrusive::value_traits<offset_node_value_traits<Node, OrderedIndex>>,
	                                           boost::intrusive::key_of_value<get_key<typename OrderedIndex::key_from_value_type, typename Node::value_type>>,
	                                           boost::intrusive::compare<typename OrderedIndex::compare_type>>;

	template<class OrderedIndex>
	constexpr bool is_valid_index = false;

	template<class... T>
	constexpr bool is_valid_index<boost::multi_index::ordered_unique<T...>> = true;

	template<class Node, class Tag>
	using list_base = boost::intrusive::slist<typename Node::value_type, boost::intrusive::value_traits<offset_node_value_traits<Node, Tag>>>;

	// Allows nested object to use a different allocator from the container.
	template<template<typename> class A, typename T>
	auto& propagate_allocator(A<T>& a) { return a; }
	template<typename T, typename S>
	auto& propagate_allocator(boost::interprocess::allocator<T, S>& a) { return a; }
	template<typename T, typename S, std::size_t N>
	auto propagate_allocator(boost::interprocess::node_allocator<T, S, N>& a) { return boost::interprocess::allocator<T, S>{a.get_segment_manager()}; }
	template<typename T, typename S, std::size_t N>
	auto propagate_allocator(boost::interprocess::private_node_allocator<T, S, N>& a) { return boost::interprocess::allocator<T, S>{a.get_segment_manager()}; }
	template<typename T, typename S>
	auto propagate_allocator(chainbase::chainbase_node_allocator<T, S>& a) { return boost::interprocess::allocator<T, S>{a.get_segment_manager()}; }

	template<class L, class It, class Pred, class Disposer>
	inline void remove_if_after_and_dispose(L& l, It it, It end, Pred&& p, Disposer&& d)
	{
		for (;;) {
			It next = it;
			++next;
			if (next == end)
				break;
			if (p(*next)) {
				l.erase_after_and_dispose(it, d);
			}
			else {
				it = next;
			}
		}
	}
}
