#pragma once

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/allocators/node_allocator.hpp>
#include <boost/interprocess/allocators/private_node_allocator.hpp>

namespace chainbase::detail {
	namespace bip = boost::interprocess;

	template<class Allocator, class T>
	using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

	// Allows nested object to use a different allocator from the container.
	template<template<class> class A, class T>
	auto& propagate_allocator(A<T>& a)
	{
		return a;
	}

	template<class T, class S>
	auto& propagate_allocator(bip::allocator<T, S>& a)
	{
		return a;
	}

	template<class T, class S, std::size_t N>
	auto propagate_allocator(bip::node_allocator<T, S, N>& a)
	{
		return bip::allocator<T, S> { a.get_segment_manager() };
	}

	template<class T, class S, std::size_t N>
	auto propagate_allocator(bip::private_node_allocator<T, S, N>& a)
	{
		return bip::allocator<T, S> { a.get_segment_manager() };
	}
}
