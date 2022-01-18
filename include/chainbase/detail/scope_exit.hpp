#pragma once

#include <utility>

namespace chainbase::detail {
	template<class F>
	struct scope_exit {
	public:
		scope_exit(F&& f)
		    : _f { std::forward<F>(f) }
		{}

		~scope_exit()
		{
			if (!_canceled)
				_f();
		}

		scope_exit(scope_exit const&) = delete;
		scope_exit& operator=(scope_exit const&) = delete;

		void cancel()
		{
			_canceled = true;
		}

	private:
		F _f;
		bool _canceled = false;
	};
}
