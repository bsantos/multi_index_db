#pragma once

#include <chainbase/name.hpp>

#include <cstdint>

namespace chainbase {
	/**
	 *  Object base class that must be inherited when implementing database objects
	 */
	template<name Name, class Derived>
	struct object {
		using id_type = uint64_t;
		static constexpr name type_name = Name;
	};
}
