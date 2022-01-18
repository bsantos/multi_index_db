#pragma once

#include <chainbase/pinnable_mapped_file.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <string>
#include <cstdint>

namespace chainbase {
	namespace bip = boost::interprocess;

	/**
	 *  ID type that uniquely identifies an object in the database
	 */
	template<size_t N>
	struct oid {
		constexpr oid(const char (&str)[N], uint16_t index)
		    : index { index }
		{
			std::copy_n(str, N, c_str);
		}

		std::string str() const
		{
			return c_str;
		}

		uint16_t index;
		char c_str[N];
	};

	/**
	 *  Object base class that must be inherited when implementing database objects
	 */
	template<oid TypeId, class Derived>
	struct object {
		using id_type = uint64_t;
		static constexpr oid type_id = TypeId;
	};

	/**
	 *  Generic standard allocator for object data members
	 */
	template<class T>
	using allocator = bip::allocator<T, pinnable_mapped_file::segment_manager>;
}
