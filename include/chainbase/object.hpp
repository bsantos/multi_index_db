#pragma once

#include <string>
#include <cstdint>

namespace chainbase {
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
}
