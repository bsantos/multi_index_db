#pragma once

#include <boost/throw_exception.hpp>

#include <cstdint>
#include <stdexcept>

namespace chainbase::detail {
	template<class Container>
	class container {
	public:
		using value_type = typename Container::value_type;
		using allocator_type = typename Container::allocator_type;

	public:
		explicit container(allocator_type const& a)
		    : _container { a }
		{}

		explicit container(Container const& other, allocator_type const& a)
		    : _container { other, a }
		{}

		auto get()
		{
			validate();
			return &_container;
		}

		auto get() const
		{
			validate();
			return &_container;
		}

	private:
		void validate() const
		{
			if (sizeof(value_type) != _size_of_value_type || sizeof(*this) != _size_of_this)
				BOOST_THROW_EXCEPTION(std::runtime_error("content of memory does not match data expected by executable"));;
		}

	private:
		Container _container;
		uint32_t _size_of_value_type = sizeof(value_type);
		uint32_t _size_of_this = sizeof(container);
	};
}
