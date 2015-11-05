/////////////////////////////////////////////////////////////////////////////// 
// 
// Copyright (c) 2015 Microsoft Corporation. All rights reserved. 
// 
// This code is licensed under the MIT License (MIT). 
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE. 
// 
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef GSL_UNIQUE_ERROR_H
#define GSL_UNIQUE_ERROR_H

#include <new>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <type_traits>
#include <utility>
#include <system_error>
#include "fail_fast.h"

#ifdef _MSC_VER

// No MSVC does constexpr fully yet
#pragma push_macro("constexpr")
#define constexpr /* nothing */


// VS 2013 workarounds
#if _MSC_VER <= 1800

// noexcept is not understood 
#ifndef GSL_THROWS_FOR_TESTING
#define noexcept /* nothing */ 
#endif

// turn off some misguided warnings
#pragma warning(push)
#pragma warning(disable: 4351) // warns about newly introduced aggregate initializer behavior

#endif // _MSC_VER <= 1800

#endif // _MSC_VER

// In order to test the library, we need it to throw exceptions that we can catch
#ifdef GSL_THROWS_FOR_TESTING
#define noexcept /* nothing */ 
#endif // GSL_THROWS_FOR_TESTING 


/*
*
* unique_error guards the lifetime of error values to ensure correct 
* error code usage. it is similar to a unique_lock and a unique_ptr.
*
* unique_error is an adapter for an existing error type and uses 
* error_traits<> specializations to define the initial success  
* value and a test for whether a value is success or an error.
*
* the primary effect of using unique_error is that any attempt to  
* reset the stored error value without checking the previous value
* will exit the process (fail_fast) - even if the value is a success
* value, it will fail_fast if the value was not checked. Thus these 
* fail_fast almost always occur immediately when running after changes 
* to the code have introduced bugs and almost never occur in shipped code.
*
* unique_error implements the policy that: not checking an error is a  
* programmer error, just like compiler errors, and that programmer errors 
* must not be recoverable at runtime, any more than compiler errors 
* are recoverable.
*
* An earlier version of this code is used in windows and vastly reduces 
* error handling bugs.
*
*/

namespace gsl {

namespace details {
struct Disposition
{
	enum type {
		Invalid = 0,

		Initiated,
		Defaulted,
		Released,
		Checked,
		Unchecked,

		End,
		Begin = Initiated
	};
};
}

template<typename T>
struct error_traits
{
	using value_type = T;
	static inline T initiate() { return T{}; }
	static inline bool ok(const T& e) { return !!e; }
};

//
// disallow ambigous integral types
//

template<>
struct error_traits<char>{};
template<>
struct error_traits<unsigned char>{};

template<>
struct error_traits<short>{};
template<>
struct error_traits<unsigned short>{};

template<>
struct error_traits<int>{};
template<>
struct error_traits<unsigned int>{};

template<>
struct error_traits<long>{};
template<>
struct error_traits<unsigned long>{};

template<>
struct error_traits<long long>{};
template<>
struct error_traits<unsigned long long>{};

template<typename Error>
class unique_error
{
public:
	using this_type = unique_error;

	using traits = error_traits<Error>;

	using type = typename traits::value_type;

	~unique_error()
	{
		reset();
	}

	unique_error()
	: value(traits::initiate())
	, disposition(details::Disposition::Defaulted)
	{
	}

	// implicitly convert from typed error
	unique_error(const type& other)
	: value(other)
	, disposition(details::Disposition::Initiated)
	{
	}
	unique_error(type&& other)
	: value(std::move(other))
	, disposition(details::Disposition::Initiated)
	{
	}

	unique_error(unique_error&& other)
	: value(std::move(other.release()))
	, disposition(details::Disposition::Unchecked)
	{
	}

	unique_error(const unique_error& other)
	: value(other.value)
	, disposition(details::Disposition::Unchecked)
	{
	}

	void swap(unique_error& other)
	{
		using std::swap;
		swap(value, other.value);
		swap(disposition, other.disposition);
	}

	unique_error& operator=(unique_error&& other)
	{
		value = std::move(other.release());
		disposition = details::Disposition::Unchecked;
		return *this;
	}
	unique_error& operator=(const unique_error& other)
	{
		value = other.value;
		disposition = details::Disposition::Unchecked;
		return *this;
	}

	explicit operator bool() const
	{
		return !ok();
	}

	this_type& reset()
	{
		reset(traits::initiate());
		disposition = details::Disposition::Defaulted;
		return *this;
	}

	this_type& reset(type raw)
	{
		fail_fast_assert(disposition != details::Disposition::Unchecked, "error was not checked");

		value = raw;
		disposition = details::Disposition::Unchecked;

		return *this;
	}

	type release()
	{
		disposition = details::Disposition::Released;
		type result = value;
		reset();
		return result;
	}

	type get() const
	{
		return value;
	}

	bool try_ok() const
	{
		return traits::ok(value);
	}

	bool ok() const
	{
		disposition = details::Disposition::Checked;
		return try_ok();
	}

private:
	friend bool operator<(const unique_error<Error>& lhs, const unique_error<Error>& rhs);

	friend bool operator==(const unique_error<Error>& lhs, const unique_error<Error>& rhs);

	friend bool operator<(const unique_error<Error>& lhs, const Error& rhs);

	friend bool operator==(const unique_error<Error>& lhs, const Error& rhs);

	friend bool operator<(const Error& lhs, const unique_error<Error>& rhs);

private:
	type value;
	mutable details::Disposition::type disposition;
};

template<typename Error>
void swap(unique_error<Error>& lhs, unique_error<Error>& rhs)
{
	lhs.swap(rhs);
}

template<typename Error>
bool operator<(const unique_error<Error>& lhs, const unique_error<Error>& rhs)
{
	return lhs.value < rhs.value;
}

template<typename Error>
bool operator==(const unique_error<Error>& lhs, const unique_error<Error>& rhs)
{
	return lhs.value == rhs.value;
}

template<typename Error>
bool operator!=(const unique_error<Error>& lhs, const unique_error<Error>& rhs)
{
	return !(lhs == rhs);
}

template<typename Error>
bool operator<(const unique_error<Error>& lhs, const Error& rhs)
{
	return lhs.value < rhs.value;
}

template<typename Error>
bool operator==(const unique_error<Error>& lhs, const Error& rhs)
{
	return lhs.value == rhs.value;
}

template<typename Error>
bool operator!=(const unique_error<Error>& lhs, const Error& rhs)
{
	return !(lhs == rhs);
}

template<typename Error>
bool operator<(const Error& lhs, const unique_error<Error>& rhs)
{
	return lhs.value < rhs.value;
}

template<typename Error>
bool operator==(const Error& lhs, const unique_error<Error>& rhs)
{
	return rhs == lhs;
}

template<typename Error>
bool operator!=(const Error& lhs, const unique_error<Error>& rhs)
{
	return !(rhs == lhs);
}


template<>
struct error_traits<std::error_condition>
{
	using value_type = std::error_condition;

	static inline std::error_condition initiate()
	{
		return std::error_condition{};
	}

	static inline bool ok(std::error_condition ge)
	{
		return !ge;
	}
};

template<>
struct error_traits<std::error_code>
{
	using value_type = std::error_code;

	static inline std::error_code initiate()
	{
		return std::error_code{};
	}

	static inline bool ok(std::error_code se)
	{
		return !se;
	}
};

}

using unique_error_condition = gsl::unique_error<std::error_condition>;
using unique_error_code = gsl::unique_error<std::error_code>;

#endif // GSL_UNIQUE_ERROR_H

/*
 * example implementation for a Win32 hresult type

enum class hresult_error : HRESULT {};

inline hresult_error hresult_cast(HRESULT raw)
{
	return static_cast<hresult_error>(raw);
}

namespace gsl {
template<>
struct error_traits<hresult_error>
{
	using value_type = hresult_error;

	static inline hresult_error initiate()
	{
		return hresult_cast(S_OK);
	}

	static inline bool ok(hresult_error hr)
	{
		return SUCCEEDED(hr);
	}
};
}

using unique_hresult = gsl::unique_error<hresult_error>;
*/


/*
 * example implementation for a Win32 winerror type

enum class winerror_error : DWORD {};

inline winerror_error winerror_cast(DWORD raw)
{
	return static_cast<winerror_error>(raw);
}

namespace gsl {
template<>
struct error_traits<winerror_error>
{
	using value_type = winerror_error;

	static inline winerror_error initiate()
	{
		return winerror_cast(ERROR_SUCCESS);
	}

	static inline bool ok(winerror_error winerror)
	{
		return winerror == winerror_cast(ERROR_SUCCESS);
	}
};
}

using unique_winerror = gsl::unique_error<winerror_error>;

inline unique_winerror make_winerror_if(BOOL is_last_error)
{
	unique_winerror result;
	if (is_last_error)
	{
		return result.reset(GetLastError());
	}
	return result;
}
*/



