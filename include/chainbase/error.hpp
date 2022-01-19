#pragma once

#include <system_error>

namespace chainbase {
	enum class errc {
		ok = 0,
		dirty,
		incompatible,
		incorrect_db_version,
		not_found,
		bad_header,
		no_access,
	};

	const std::error_category& error_category();

	inline std::error_code make_error_code(errc e) noexcept
	{
		return std::error_code(static_cast<int>(e), error_category());
	}

	class error_category : public std::error_category {
	public:
		const char* name() const noexcept override;
		std::string message(int ev) const override;
	};
}

namespace std {
	template<>
	struct is_error_code_enum<::chainbase::errc> : std::true_type {};
}
