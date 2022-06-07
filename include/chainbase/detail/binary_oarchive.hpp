#pragma once

#include <chainbase/detail/utility.hpp>

#include <boost/crc.hpp>
#include <boost/mpl/bool.hpp>
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
#include <ostream>
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
	class binary_oarchive {
	public:
		using is_saving = boost::mpl::bool_<true>;
		using is_loading = boost::mpl::bool_<false>;

	public:
		explicit binary_oarchive(std::ostream& out)
			: _out { out }
		{}

		template<class T>
		void save(T const& value)
		{
			if constexpr (bs::implementation_level<T>::value == bs::primitive_type)
				serialize_primitive(value);
			else if constexpr (bs::implementation_level<T>::value != bs::not_serializable)
				serialize_object(value);
		}

		void save(bool value) { serialize_unsigned_integral(static_cast<uint8_t>(value)); }
		void save(unsigned char value) { serialize_unsigned_integral(value); }
		void save(unsigned short value) { serialize_unsigned_integral(value); }
		void save(unsigned int value) { serialize_unsigned_integral(value); }
		void save(unsigned long value) { serialize_unsigned_integral(value); }
		void save(unsigned long long value) { serialize_unsigned_integral(value); }

		template<class T, size_t N>
		void save(T (&value)[N])
		{
			for (auto&& item : value)
				save(item);
		}

		template<class T, size_t N>
		void save(std::array<T, N> const& value)
		{
			for (auto&& item : value)
				save(item);
		}

		void save(std::string const& value)
		{
			assert(static_cast<uint32_t>(value.size()) == value.size());
			serialize_unsigned_integral(static_cast<uint32_t>(value.size()));
			serialize_bytes(reinterpret_cast<uint8_t const*>(value.data()), value.size());
		}

		void save(bs::item_version_type value)
		{
			assert(static_cast<uint32_t>(value) == value);
			save(static_cast<uint32_t>(value));
		}

		void save(bs::collection_size_type value)
		{
			assert(static_cast<uint32_t>(value) == value);
			save(static_cast<uint32_t>(value));
		}

		template<class T>
		binary_oarchive& operator<<(T const& value)
		{
			save(value);
			return *this;
		}

		template<class T>
		binary_oarchive& operator&(T const& value)
		{
			save(value);
			return *this;
		}

		[[nodiscard]] uint32_t checksum()
		{
			auto crc = _crc.checksum();
			_crc.reset();
			return crc;
		}

	private:
		template<class T>
		void serialize_primitive(T value)
		{
			using no_cvr_type = std::remove_cv_t<std::remove_reference_t<T>>;
			static_assert(std::is_integral_v<no_cvr_type> && std::is_signed_v<no_cvr_type>);

			serialize_unsigned_integral(static_cast<std::make_unsigned_t<no_cvr_type>>(value));
		}

		template<class T, std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>* = nullptr>
		void serialize_unsigned_integral(T value)
		{
			value = big_endian_order(value);
			serialize_bytes(&value, sizeof(value));
		}

		template<class T>
		void serialize_object(T const& obj)
		{
			bs::serialize_adl(*this, const_cast<T&>(obj), bs::version<T>::value);
		}

		void serialize_bytes(void const* data, size_t size)
		{
			_crc.process_bytes(data, size);

			if (!_out.get().write(static_cast<char const*>(data), size))
				BOOST_THROW_EXCEPTION(std::runtime_error("output write error"));
		}

	private:
		std::reference_wrapper<std::ostream> _out;
		boost::crc_32_type _crc;
	};
}
