#pragma once

#include <chainbase/name.hpp>

#include <cstdint>

namespace chainbase {
	/**
	 *  Object base class that must be inherited when implementing database objects
	 */
	template<name Name, uint16_t TypeId, class Derived>
	struct object {
		using id_type = uint64_t;
		static constexpr name type_name = Name;
		static constexpr uint16_t type_id = TypeId;
	};
}
