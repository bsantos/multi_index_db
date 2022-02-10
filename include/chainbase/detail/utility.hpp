#pragma once

#include <bit>
#include <type_traits>

namespace chainbase::detail {
	template<class T, std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>* = nullptr>
	[[nodiscard]]
	constexpr T byteswap(T n) noexcept
	{
		static_assert(sizeof(T) <= 8, "no support for byteswap for this integer size");

		if constexpr (sizeof(T) == 2)
			return __builtin_bswap16(n);
		else if constexpr (sizeof(T) == 4)
			return __builtin_bswap32(n);
		else if constexpr (sizeof(T) == 8)
			return __builtin_bswap64(n);

		return n;
	}

	template<class T>
	[[nodiscard]]
	constexpr T big_endian_order(T n) noexcept
	{
		if constexpr (std::endian::native == std::endian::little)
			return byteswap(n);

		return n;
	}

	template<class T>
	constexpr auto to_underlying(T v) noexcept -> std::underlying_type_t<T>
	{
		return static_cast<std::underlying_type_t<T>>(v);
	}

	template<class T>
	constexpr auto from_underlying(std::underlying_type_t<T> v) noexcept
	{
		return static_cast<T>(v);
	}
}
