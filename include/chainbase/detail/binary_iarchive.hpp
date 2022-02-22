#pragma once

#include <chainbase/detail/utility.hpp>

#include <boost/crc.hpp>
#include <boost/mpl/bool.hpp>
#include <boost/throw_exception.hpp>
#include <boost/serialization/level.hpp>
#include <boost/serialization/version.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/item_version_type.hpp>
#include <boost/serialization/library_version_type.hpp>
#include <boost/serialization/collection_size_type.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <istream>
#include <algorithm>
#include <type_traits>

namespace chainbase::detail {
	namespace bs = boost::serialization;

	/**
	 * Portable output binary archive serializer
	 *
	 * This archive partialy implements Boost.Archive concept. Object class
	 * info is not fully supported, meaning it's redirected to object
	 * serialization code path. In other words, we don't support shared_ptr's
	 * because we don't have object tracking and we also don't have type
	 * registration, so derived classes in pointers are not supported either,
	 * pointers must always point to the final type.
	 */
	class binary_iarchive {
	public:
		using is_saving = boost::mpl::bool_<false>;
		using is_loading = boost::mpl::bool_<true>;

	public:
		explicit binary_iarchive(std::istream& in)
			: _in { in }
		{}

		template<class T>
		void load(T& value)
		{
			if constexpr (bs::implementation_level<T>::value == bs::primitive_type)
				serialize_primitive(value);
			else if constexpr (bs::implementation_level<T>::value != bs::not_serializable)
				serialize_object(value);
		}

		void load(bool& value) { value = static_cast<bool>(serialize_unsigned_integral<uint8_t>()); }
		void load(unsigned char& value) { value = serialize_unsigned_integral<unsigned char>(); }
		void load(unsigned short& value) { value = serialize_unsigned_integral<unsigned short>(); }
		void load(unsigned int& value) { value = serialize_unsigned_integral<unsigned int>(); }
		void load(unsigned long& value) { value = serialize_unsigned_integral<unsigned long>(); }
		void load(unsigned long long& value) { value = serialize_unsigned_integral<unsigned long long>(); }

		template<class T, size_t N>
		void load(T (&value)[N])
		{
			for (auto& item : value)
				load(item);
		}

		void load(std::string& value)
		{
			auto size = serialize_unsigned_integral<uint32_t>();
			value.resize(size);
			serialize_bytes(value.data(), size);
		}

		void load(bs::item_version_type& value) { value = bs::item_version_type { serialize_unsigned_integral<uint32_t>() }; }
		void load(bs::collection_size_type& value) { value = bs::collection_size_type { serialize_unsigned_integral<uint32_t>() }; }

		template<class T>
		binary_iarchive& operator>>(T& v)
		{
			load(v);
			return *this;
		}

		template<class T>
		binary_iarchive& operator&(T& v)
		{
			load(v);
			return *this;
		}

		bs::library_version_type get_library_version() const { return {}; }

		[[nodiscard]] uint32_t checksum()
		{
			auto crc = _crc.checksum();
			_crc.reset();
			return crc;
		}

		void skip(size_t size)
		{
			char buff[1024];

			for (size_t i = 0; i < size; i += sizeof(buff))
				serialize_bytes(buff, std::min(size - i, sizeof(buff)));
		}

	private:
		template<class T>
		void serialize_primitive(T& value)
		{
			using no_cvr_type = std::remove_cv_t<std::remove_reference_t<T>>;
			static_assert(std::is_integral_v<no_cvr_type> && std::is_signed_v<no_cvr_type>);

			value = static_cast<no_cvr_type>(serialize_unsigned_integral<std::make_unsigned_t<no_cvr_type>>());
		}

		template<class T, std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>* = nullptr>
		T serialize_unsigned_integral()
		{
			T value;

			serialize_bytes(&value, sizeof(value));
			return big_endian_order(value);
		}

		template<class T>
		void serialize_object(T const& obj)
		{
			bs::serialize_adl(*this, const_cast<T&>(obj), bs::version<T>::value);
		}

		void serialize_bytes(void* data, size_t size)
		{
			if (!_in.get().read(static_cast<char*>(data), size))
				BOOST_THROW_EXCEPTION(std::runtime_error("input read error"));

			_crc.process_bytes(data, size);
		}

	private:
		std::reference_wrapper<std::istream> _in;
		boost::crc_32_type _crc;
	};
}
