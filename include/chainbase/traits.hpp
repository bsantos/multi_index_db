#pragma once

#include <type_traits>

namespace chainbase {
	template<class T, class A>
	class singleton;

	template<class T, class A, class... I>
	class basic_multi_index;

	template<class T, class A, class... I>
	class basic_undo_multi_index;

	template<class T>
	struct is_singleton : std::false_type {};

	template<class T, class A>
	struct is_singleton<singleton<T, A>> : std::true_type {};

	template<class T>
	struct is_multi_index : std::false_type {};

	template<class T, class A, class... I>
	struct is_multi_index<basic_multi_index<T, A, I...>> : std::true_type {};

	template<class T>
	struct is_undo_multi_index : std::false_type {};

	template<class T, class A, class... I>
	struct is_undo_multi_index<basic_undo_multi_index<T, A, I...>> : std::true_type {};

	template<class T>
	inline constexpr bool is_singleton_v = is_singleton<T>::value;

	template<class T>
	inline constexpr bool is_multi_index_v = is_multi_index<T>::value;

	template<class T>
	inline constexpr bool is_undo_multi_index_v = is_undo_multi_index<T>::value;

	template<class T>
	inline constexpr bool is_container_v = is_singleton_v<T> || is_multi_index_v<T> || is_undo_multi_index_v<T>;
}
