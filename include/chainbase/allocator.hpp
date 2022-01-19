#pragma once

#include <boost/interprocess/sync/mutex_family.hpp>
#include <boost/interprocess/indexes/iset_index.hpp>
#include <boost/interprocess/mem_algo/rbtree_best_fit.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace chainbase {
	namespace bip = boost::interprocess;

	/**
	 *  Memory manager for the database memory mapped file
	 */
	using segment_manager = bip::segment_manager<char, bip::rbtree_best_fit<bip::null_mutex_family>, bip::iset_index>;

	/**
	 *  Generic standard allocator for object data members
	 */
	template<class T>
	using allocator = bip::allocator<T, segment_manager>;
}
