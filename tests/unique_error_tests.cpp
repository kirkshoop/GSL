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

#include <UnitTest++/UnitTest++.h> 
#include <gsl.h>
#include <functional>

using namespace gsl;

/*
* indexer - sugar for indexing into compile-time lists
*   std::get<0>(tupl) == 0_idx[tupl]
*/
template<int i>
struct indexer : public std::integral_constant<int, i>
{
	template<typename T>
	auto operator[](T&& t) const -> decltype(std::get<i>(std::forward<T>(t))) {
		return std::get<i>(std::forward<T>(t));
	}
};

template<class T>
constexpr T make(T acc) { return acc; }
template<class T, char c, char...cn>
constexpr T make(T acc) { return make<int, cn...>((acc * 10) + (c - '0')); }

template<char... cn>
constexpr  indexer<make<int, cn...>(0)> operator "" _idx () {
	return indexer<make<int, cn...>(0)>();
}

/*
* need to call nothrow new to show off unique_error
*/
template<typename T, class... UN>
std::unique_ptr<T> make_unique_nothrow(UN&&... un) {
	return std::unique_ptr<T>{new(std::nothrow) T{std::forward<UN>(un)...}};
}

/*
*simplify usage of unique_error for unique_ptr.
*/
template<typename T>
unique_error_condition check_unique_ptr(const std::unique_ptr<T>& p) {
	if (!p) return std::make_error_condition(std::errc::not_enough_memory);
	return unique_error_condition{};
}

/*
*simple/incomplete either type
*/
template<typename T>
struct either_ok
{
	using value_type = T;
	T t;
};
template<typename T>
either_ok<std::decay_t<T>> ok(T&& t) {
	return either_ok<std::decay_t<T>>{std::forward<T>(t)};
}
template<>
struct either_ok<void>
{
	using value_type = void;
};
either_ok<void> ok() {
	return either_ok<void>{};
}

template<typename Error>
struct either_fail
{
	using value_type = void;
	Error e;
};
template<typename T>
either_fail<std::decay_t<T>> fail(T&& t) {
	return either_fail<std::decay_t<T>>{std::forward<T>(t)};
}

template<typename Error, typename T>
class either
{
	mutable Error e;
	T t;
public:
	using value_type = T;
	either() = default;
	either(either&&) = default;
	either(const either&) = default;
	either(either_fail<Error>&& o) : e(std::move(o.e)){}
	either(either_ok<T>&& o) : t(std::move(o.t)){}
	either& operator=(either o){
		e = std::move(o.e);
		t = std::move(o.t);
		return *this;
	}
	Error& error() const {return e;}
	template<typename F>
	auto ifok(F&& f) -> either<Error, typename decltype(f(std::move(t)))::value_type>{
		if (e) {
			return fail(e);
		}
		return f(std::move(t));
	}
	template<typename F>
	auto ifok(F&& f) const -> either<Error, typename decltype(f(t))::value_type>{
		if (e) {
			return fail(e);
		}
		return f(t);
	}
};
template<typename Error>
class either<Error, void>
{
	mutable Error e;
public:
	using value_type = void;
	either() = default;
	either(either&&) = default;
	either(const either&) = default;
	either(either_fail<Error>&& o) : e(std::move(o.e)){}
	either(either_ok<void>&& o) {}
	either& operator=(either o){
		e = std::move(o.e);
		return *this;
	}
	Error& error() const {return e;}
	template<typename F>
	auto ifok(F&& f) -> either<Error, void>{
		if (e) {
			return fail(e);
		}
		return f();
	}
	template<typename F>
	auto ifok(F&& f) const -> either<Error, void>{
		if (e) {
			return fail(e);
		}
		return f();
	}
};

template<typename T, class... UN>
auto make_unique_or_error_condition(UN&&... un) -> either<unique_error_condition, std::unique_ptr<T>>
{
	auto t = make_unique_nothrow<T>(std::forward<UN>(un)...);
	auto ge = check_unique_ptr(t);
	if (ge) return fail(ge);
	return ok(std::move(t));
}

SUITE(unique_error_tests)
{
	struct Foo {void foo() {}};
	struct Bar {void bar() {}};

	std::tuple<unique_error_condition, unique_ptr<Foo>, unique_ptr<Bar>> Create1() noexcept {
		unique_error_condition ge;
		unique_ptr<Foo> foo;
		unique_ptr<Bar> bar;
		auto result = [&]{return std::make_tuple(std::move(ge), std::move(foo), std::move(bar));};

		foo = make_unique_nothrow<Foo>();
		ge = check_unique_ptr(foo);
		if (ge) return result();

		bar = make_unique_nothrow<Bar>();
		ge = check_unique_ptr(bar);
		if (ge) return result();

		return result();
	}

	either<unique_error_condition, std::tuple<std::unique_ptr<Foo>, std::unique_ptr<Bar>>> Create2() noexcept {
		return make_unique_or_error_condition<Foo>().
			ifok([](auto foo){
				return make_unique_or_error_condition<Bar>().
					ifok([&](auto bar){
						return ok(std::make_tuple(std::move(foo), std::move(bar)));
					});
			});
	}

	unique_error_condition okcall(){
		return unique_error_condition{};
	}
	unique_error_condition failcall(){
		return unique_error_condition{std::make_error_condition(std::errc::invalid_argument)};
	}

	std::error_condition okApiCall(){
		return std::error_condition{};
	}
	std::error_condition failApiCall(){
		return std::make_error_condition(std::errc::invalid_argument);
	}

	TEST(create1_test)
	{
		auto result = Create1();
		CHECK(0_idx[result].try_ok());
		if (!0_idx[result]) {
			CHECK(!!1_idx[result]);
			1_idx[result]->foo();
			CHECK(!!2_idx[result]);
			2_idx[result]->bar();
		}
	}

	TEST(create2_test)
	{
		bool called = false;
		auto ge = Create2().
			ifok([&](auto foobar){
				CHECK(!!0_idx[foobar]);
				0_idx[foobar]->foo();
				CHECK(!!1_idx[foobar]);
				1_idx[foobar]->bar();
				called = true;
				return ok();
			}).
			error().
			release();
		CHECK(called);
		CHECK(!ge);
	}

	TEST(error_ge_basic_test)
	{
		unique_error_condition ge;
		CHECK(ge.try_ok());
		// default is safe
	}

	TEST(error_ge_missing_ok_test)
	{
		unique_error_condition ge;
		// default is safe

		ge.reset(okApiCall());
		// unssafe after calling api 

		// boom
		CHECK_THROW(ge.reset(okApiCall()), fail_fast);

		ge.release();
		// made safe for destruction
	}

	TEST(error_ge_missing_fail_test)
	{
		unique_error_condition ge{std::make_error_condition(std::errc::invalid_argument)};
		CHECK(!ge.try_ok());
		// initialized is safe

		ge.reset(failApiCall());
		CHECK(!ge.try_ok());
		// unssafe after calling api 

		// boom
		CHECK_THROW(ge.reset(failApiCall()), fail_fast);

		ge.release();
		// made safe for destruction
	}

	TEST(error_ge_long_test)
	{
		unique_error_condition ge;
		CHECK(ge.try_ok());
		// default is safe

		ge.reset(okApiCall());
		CHECK(ge.try_ok());
		// unssafe after calling api 

		// boom
		CHECK_THROW(ge.reset(okApiCall()), fail_fast);

		if (ge) {}
		// made safe by bool operatoor test

		ge = okcall();
		CHECK(ge.try_ok());
		CHECK(ge.get() == std::error_condition{});
		// unssafe after okcall

		// boom
		CHECK_THROW(ge.reset(okApiCall()), fail_fast);

		ge.release();
		// made safe by releasing

		ge = failcall();
		CHECK(!ge.try_ok());
		CHECK(ge.get() == std::make_error_condition(std::errc::invalid_argument));
		// unssafe after failcall

		// boom
		CHECK_THROW(ge.reset(failApiCall()), fail_fast);

		ge.release();
		// made safe by releasing

		ge.reset();
		CHECK(ge.try_ok());
		// still safe after clearing with reset()

		ge = unique_error_condition{std::make_error_condition(std::errc::not_enough_memory)};
		CHECK(!ge.try_ok());
		// unssafe after copy

		// boom
		CHECK_THROW(ge.reset(failApiCall()), fail_fast);

		if (ge.ok()) {}
		// made safe by ok test
	}
}

int main(int, const char *[])
{
	return UnitTest::RunAllTests();
}
