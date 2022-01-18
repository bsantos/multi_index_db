#pragma once

#include <chainbase/pinnable_mapped_file.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace chainbase {
	namespace bip = boost::interprocess;

	/**
	 *  Generic standard allocator for object data members
	 */
	template<class T>
	using allocator = bip::allocator<T, pinnable_mapped_file::segment_manager>;
}
