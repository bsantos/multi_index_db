#pragma once

#include <boost/throw_exception.hpp>

#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace chainbase {
	template<class T, class A>
	class singleton;

	template<class T, class A, class... I>
	class basic_multi_index;

	template<class T, class A, class... I>
	class basic_undo_multi_index;
}

namespace chainbase::detail {
	template<class T>
	struct is_container : std::false_type {};

	template<class T, class A>
	struct is_container<singleton<T, A>> : std::true_type {};

	template<class T, class A, class... I>
	struct is_container<basic_multi_index<T, A, I...>> : std::true_type {};

	template<class T, class A, class... I>
	struct is_container<basic_undo_multi_index<T, A, I...>> : std::true_type {};

	template<class T>
	inline constexpr bool is_container_v = is_container<T>::value;

	template<class Container>
	class container {
	public:
		using value_type = typename Container::value_type;
		using allocator_type = typename Container::allocator_type;

		static_assert(is_container_v<Container>);

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
