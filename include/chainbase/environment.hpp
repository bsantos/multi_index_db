#pragma once

#include <boost/version.hpp>

#include <iomanip>
#include <cstdint>
#include <cstring>

namespace chainbase {
	constexpr size_t header_size = 384;
	constexpr uint64_t header_id = 0x4277554c54494442ULL; // "BMULTIDB"

	struct environment {
		environment()
		{
			std::strncpy(compiler, __VERSION__, sizeof(compiler) - 1);
		}

		enum os_t : uint8_t {
			OS_LINUX,
			OS_MACOS,
			OS_WINDOWS,
		};

		enum arch_t : uint8_t {
			ARCH_X86_64,
			ARCH_ARM,
		};

		uint8_t debug =
#ifndef NDEBUG
		    true;
#else
		    false;
#endif

		os_t os =
#if defined(__linux__)
		    OS_LINUX;
#elif defined(__APPLE__)
		    OS_MACOS;
#elif defined(_WIN32)
		        OS_WINDOWS;
#else
#	error "unknown os"
#endif

		arch_t arch =
#if defined(__x86_64__)
		    ARCH_X86_64;
#elif defined(__aarch64__)
		    ARCH_ARM64;
#else
#	error "unknown architecture"
#endif

		uint32_t boost_version = BOOST_VERSION;
		char compiler[256] = {};

		bool operator==(environment const& other) const
		{
			return !std::memcmp(this, &other, sizeof(environment));
		}

		bool operator!=(environment const& other) const
		{
			return !(*this == other);
		}

		std::string str() const;

	} __attribute__((packed));

	struct db_header {
		uint64_t id = header_id;
		uint32_t size = header_size;
		uint8_t dirty = false;
		environment dbenviron;
	} __attribute__((packed));

	constexpr size_t header_dirty_bit_offset = offsetof(db_header, dirty);

	static_assert(sizeof(db_header) <= header_size, "DB header struct too large");

	std::ostream& operator<<(std::ostream& os, const chainbase::environment& dt);
}
