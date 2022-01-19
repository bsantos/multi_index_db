#pragma once

#include <string>
#include <string_view>
#include <algorithm>

namespace chainbase {
	/**
	 *  Name type to identify containers in the database
	 */
	template<size_t N>
	struct name {
		constexpr name(const char (&str)[N]) noexcept
		{
			std::copy_n(str, N, chars);
		}

		constexpr char const* c_str() const noexcept
		{
			return chars;
		}

		constexpr std::string_view view() const noexcept
		{
			return { chars, size() };
		}

		std::string str() const
		{
			return { chars, size() };
		}

		constexpr char const* begin() const noexcept { return chars; }
		constexpr char const* end() const noexcept { return chars + size(); }
		constexpr size_t size() const noexcept { return N - 1; }

		char chars[N];
	};
}
