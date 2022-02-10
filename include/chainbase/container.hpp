#pragma once

#include <type_traits>

namespace chainbase {
	template<class T, class A>
	class singleton;

	template<class T, class A, class... I>
	class basic_multi_index;

	template<class T, class A, class... I>
	class basic_undo_multi_index;

	template<class C>
	class journaled;

	template<class T>
	struct is_container : std::false_type {};

	template<class T, class A>
	struct is_container<singleton<T, A>> : std::true_type {};

	template<class T, class A, class... I>
	struct is_container<basic_multi_index<T, A, I...>> : std::true_type {};

	template<class T, class A, class... I>
	struct is_container<basic_undo_multi_index<T, A, I...>> : std::true_type {};

	template<class T>
	struct is_journaled : std::false_type {};

	template<class T, class A, class... I>
	struct is_journaled<journaled<basic_multi_index<T, A, I...>>> : std::true_type {};

	template<class T, class A, class... I>
	struct is_journaled<journaled<basic_undo_multi_index<T, A, I...>>> : std::true_type {};

	template<class T>
	inline constexpr bool is_container_v = is_container<T>::value;

	template<class T>
	inline constexpr bool is_journaled_v = is_journaled<T>::value;
}
