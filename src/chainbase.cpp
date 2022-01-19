#include <chainbase/error.hpp>
#include <chainbase/environment.hpp>

#include <sstream>

namespace chainbase {
	const char* error_category::name() const noexcept
	{
		return "chainbase";
	}

	std::string error_category::message(int ev) const
	{
		switch (static_cast<errc>(ev)) {
		case errc::ok: return "Ok";
		case errc::dirty: return "Database dirty flag set";
		case errc::incompatible: return "Database incompatible; All environment parameters must match";
		case errc::incorrect_db_version: return "Database format not compatible with this version of chainbase";
		case errc::not_found: return "Database file not found";
		case errc::bad_header: return "Failed to read DB header";
		case errc::no_access: return "Could not gain write access to the shared memory file";
		default: return "Unrecognized error code";
		}
	}

	const std::error_category& error_category()
	{
		static const class error_category the_category;
		return the_category;
	}

	static std::string_view print_os(environment::os_t os)
	{
		switch (os) {
		case environment::OS_LINUX: return "Linux";
		case environment::OS_MACOS: return "macOS";
		case environment::OS_WINDOWS: return "Windows";
		}
		return {};
	}

	static std::string_view print_arch(environment::arch_t arch)
	{
		switch (arch) {
		case environment::ARCH_X86_64: return "x86_64";
		case environment::ARCH_ARM: return "ARM";
		}
		return {};
	}

	std::string chainbase::environment::str() const
	{
		std::stringstream ss;

		ss << *this;
		return ss.str();
	}

	std::ostream& operator<<(std::ostream& os, const chainbase::environment& dt)
	{
		os << std::right << std::setw(17) << "Compiler: " << dt.compiler << std::endl;
		os << std::right << std::setw(17) << "Debug: " << (dt.debug ? "Yes" : "No") << std::endl;
		os << std::right << std::setw(17) << "OS: " << print_os(dt.os) << std::endl;
		os << std::right << std::setw(17) << "Arch: " << print_arch(dt.arch) << std::endl;
		os << std::right << std::setw(17) << "Boost: " << dt.boost_version / 100000 << "." << dt.boost_version / 100 % 1000 << "." << dt.boost_version % 100
		   << std::endl;
		return os;
	}
}
