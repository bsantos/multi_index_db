#pragma once

#include <chainbase/detail/allocator.hpp>

#include <boost/throw_exception.hpp>

#include <cstddef>
#include <cstdint>

namespace chainbase {
	/**
	 *  Singleton container
	 */
	template<class Object, class Allocator>
	class singleton {
		singleton(singleton const&) = delete;
		singleton& operator=(singleton const&) = delete;

	public:
		using value_type = Object;
		using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<Object>;;
		using alloc_traits = typename std::allocator_traits<Allocator>::template rebind_traits<Object>;
		using pointer = value_type*;
		using const_pointer = value_type const*;
		using reference = value_type&;
		using const_reference = value_type const&;

	public:
		explicit singleton(Allocator const& a)
		    : _allocator { a }
		{}

		explicit singleton(singleton const& other, Allocator const& a)
		    : _allocator { a }
		{
			if (other)
				emplace([&](reference v) { v = *other.as_value(); });
		}

		~singleton() noexcept
		{
			clear();
		}

		template<class Constructor>
		reference emplace(Constructor&& c)
		{
			alloc_traits::construct(_allocator, as_value(), std::forward<Constructor>(c), detail::propagate_allocator(_allocator));
			_constructed = true;
			return *as_value();
		}

		pointer get()
		{
			return _constructed ? as_value() : nullptr;
		}

		const_pointer get() const
		{
			return _constructed ? as_value() : nullptr;
		}

		template<class Constructor>
		reference get_or_construct(Constructor&& c)
		{
			if (!_constructed)
				return emplace(std::forward<Constructor>(c));

			return *as_value();
		}

		void clear()
		{
			if (operator bool()) {
				alloc_traits::destroy(_allocator, as_value());
				_constructed = false;
			}
		}

		explicit operator bool() const { return _constructed; }

		reference operator*()
		{
			assert(_constructed);
			return *as_value();
		}

		const_reference operator*() const
		{
			assert(_constructed);
			return *as_value();
		}

		pointer operator->()
		{
			assert(_constructed);
			return as_value();
		}

		const_pointer operator->() const
		{
			assert(_constructed);
			return as_value();
		}

	private:
		pointer as_value() const { return reinterpret_cast<pointer>(_storage); }

	private:
		alignas(alignof(value_type)) std::byte _storage[sizeof(value_type)];
		allocator_type _allocator;
		bool _constructed { false };
	};
}
