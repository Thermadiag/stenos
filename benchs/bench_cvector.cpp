/**
 * MIT License
 *
 * Copyright (c) 2025 Victor Moncada <vtr.moncada@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string>
#include <vector>
#include <deque>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <list>
#include <stenos/cvector.hpp>
#include <stenos/timer.hpp>


#ifdef max
#undef min
#undef max
#endif

#define STENOS_TEST(...)                                                                                                                                                                               \
	if (!(__VA_ARGS__))                                                                                                                                                                            \
	STENOS_ABORT("Test error in %s line %i\n", __FILE__, __LINE__)


template<typename Ch, typename Traits = std::char_traits<Ch>>
struct basic_nullbuf : std::basic_streambuf<Ch, Traits>
{
	using base_type = std::basic_streambuf<Ch, Traits>;
	using int_type = typename base_type::int_type;
	using traits_type = typename base_type::traits_type;

	virtual auto overflow(int_type c) -> int_type override { return traits_type::not_eof(c); }
};

	/// @brief For tests only, alias for null buffer, to be used with c++ iostreams
	using nullbuf = basic_nullbuf<char>;
template<class T>
void print_null(const T& v)
{
	static nullbuf n;
	auto b = std::cout.rdbuf(&n);
	std::cout << v << std::endl;
	std::cout.rdbuf(b);
}

template<class... Args>
static inline std::string as_aligned_string(size_t width, const char* format, Args&&... args)
{
	char buffer[100];
	snprintf(buffer, sizeof(buffer), format, std::forward<Args>(args)...);

	auto len = strlen(buffer);
	if (len >= width)
		return std::string(buffer).substr(0, width);

	auto len2 = (width - len) / 2;
	std::string str(buffer);
	str.insert(0, len2, ' ');
	str.insert(str.size(), (width - len) - len2, ' ');
	return str;
}

template<class T1, class T2, class T3>
void write_line_generic(std::ostream & oss, const char * operation, const char *format, const T1 & t1, const T2 & t2, const T3 & t3)
{
	static constexpr size_t width = 20;
	oss << "|" << as_aligned_string(width * 2, "%s", operation) << "|" << as_aligned_string(width, format, t1) << "|" << as_aligned_string(width, format, t2) << "|"
	    << as_aligned_string(width, format, t3)
		  << "|" ;
}

void write_header()
{
	std::ostringstream oss;
	write_line_generic(oss, "Operation", "%s", "std::vector", "std::deque", "stenos::cvector");

	std::cout << oss.str() << std::endl;
	std::string line(oss.str().size(), '-');
	std::cout << line << std::endl;
}


void write_line(const char* operation, size_t ms1, size_t ms2, size_t ms3)
{
	write_line_generic(std::cout, operation, "%d", (int)ms1, (int)ms2, (int)ms3);
	std::cout << std::endl;
}

static stenos::timer _timer;

static void tick()
{
	_timer.tick();
}
static size_t tock_ms()
{
	return (size_t)( _timer.tock() * 1e-6);
}


template<class Deq1, class Deq2>
void assert_equal(const Deq1& d1, const Deq2& d2)
{
	if (d1.size() != d2.size())
		throw std::runtime_error("different size!");
	if (d1.size() == 0)
		return;
	if (d1.front() != d2.front())
		throw std::runtime_error("different front!");
	if (d1.back() != d2.back()) {
		throw std::runtime_error("different back!");
	}
	auto it1 = d1.begin();
	auto it2 = d2.begin();
	while (it1 != d1.end()) {
		if (*it1 != *it2) {
			throw std::runtime_error("");
		}
		++it1;
		++it2;
	}
}


/// @brief Compare performances of std::vector, std::deque, seq::tiered_vector and seq::devector
/// A value of 1000000000 means that the container has not been tested against a particular operation because too slow (for instance pop front on a std::vector).
template<class T>
void bench(size_t count = 10000000)
{
	std::cout << std::endl;
	std::cout << "Compare performances of std::vector, std::deque and stenos::cvector" << std::endl;
	std::cout << std::endl;

	write_header();
	{
		typedef T type;
		std::vector<type> vec;
		std::deque<type> deq;

		using deque_type = stenos::cvector<type>;
		deque_type tvec;

		size_t vec_t, deq_t, tvec_t, cvec_t;

		tick();
		for (size_t i = 0; i < count; ++i)
			deq.push_back(i);
		deq_t = tock_ms();

		tick();
		for (size_t i = 0; i < count; ++i)
			vec.push_back(i);
		vec_t = tock_ms();

		tick();
		for (size_t i = 0; i < count; ++i)
			tvec.push_back(i);
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("push_back", vec_t, deq_t, tvec_t);

		
		deq = std::deque<type>{};
		vec = std::vector<type>{};
		tvec = deque_type{};

		for (size_t i = 0; i < count; ++i) {
			deq.push_back(i);
			vec.push_back(i);
			tvec.push_back(i);
		}

		size_t sum = 0, sum2 = 0, sum3 = 0;
		tick();
		for (size_t i = 0; i < count; ++i)
			sum += deq[i];
		deq_t = tock_ms();
		print_null(sum);

		tick();
		sum = 0;
		for (size_t i = 0; i < count; ++i)
			sum += vec[i];
		vec_t = tock_ms();
		print_null(sum);

		tick();
		sum2 = 0;
		for (size_t i = 0; i < count; ++i)
			sum2 += tvec[i];
		tvec_t = tock_ms();
		print_null(sum2);

		STENOS_TEST(sum == sum2);
		write_line("iterate operator[]", vec_t, deq_t, tvec_t);

		sum = 0;
		tick();
		for (typename std::deque<type>::iterator it = deq.begin(); it != deq.end(); ++it)
			sum += *it;
		deq_t = tock_ms();
		print_null(sum);

		tick();
		sum = 0;
		for (auto it = vec.begin(); it != vec.end(); ++it)
			sum += *it;
		vec_t = tock_ms();
		print_null(sum);

		tick();
		sum2 = 0;
		auto end = tvec.cend();
		for (auto it = tvec.cbegin(); it != end; ++it) {
			sum2 += *it;
		}
		tvec_t = tock_ms();
		print_null(sum2);

		STENOS_TEST(sum == sum2);
		write_line("iterate iterators", vec_t, deq_t, tvec_t) ;

		tick();
		deq.resize(deq.size() / 10);
		deq_t = tock_ms();

		tick();
		vec.resize(vec.size() / 10);
		vec_t = tock_ms();

		tick();
		tvec.resize(tvec.size() / 10);
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("resize to lower", vec_t, deq_t, tvec_t);

		tick();
		deq.resize(count, 0);
		deq_t = tock_ms();

		tick();
		vec.resize(count, 0);
		vec_t = tock_ms();

		tick();
		tvec.resize(count, 0);
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("resize to upper", vec_t, deq_t, tvec_t);

		{
			tick();
			std::deque<type> d2 = deq;
			deq_t = tock_ms();

			tick();
			std::vector<type> v2 = vec;
			vec_t = tock_ms();

			tick();
			deque_type dd2 = tvec;
			tvec_t = tock_ms();

			assert_equal(d2, dd2);
			write_line("copy construct", vec_t, deq_t, tvec_t) ;
		}

		assert_equal(deq, tvec);

		{
			std::vector<type> tmp = vec;

			tick();
			deq.insert(deq.begin() + (deq.size() * 2) / 5, tmp.begin(), tmp.end());
			deq_t = tock_ms();

			tick();
			vec.insert(vec.begin() + (vec.size() * 2) / 5, tmp.begin(), tmp.end());
			vec_t = tock_ms();

			tick();
			tvec.insert(tvec.begin() + (tvec.size() * 2) / 5, tmp.begin(), tmp.end());
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("insert range left side", vec_t, deq_t, tvec_t) ;

			deq.resize(count);
			tvec.resize(count);
			vec.resize(count);

			assert_equal(deq, tvec);

			// TODO

			tick();
			deq.insert(deq.begin() + (deq.size() * 3) / 5, tmp.begin(), tmp.end());
			deq_t = tock_ms();

			tick();
			vec.insert(vec.begin() + (vec.size() * 3) / 5, tmp.begin(), tmp.end());
			vec_t = tock_ms();

			tick();
			tvec.insert(tvec.begin() + (tvec.size() * 3) / 5, tmp.begin(), tmp.end());
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("insert range right side", vec_t, deq_t, tvec_t);

			deq.resize(count);
			vec.resize(count);
			tvec.resize(count);
		}

		{
			for (size_t i = 0; i < deq.size(); ++i) {
				deq[i] = vec[i] = tvec[i] = i;
			}
			assert_equal(deq, tvec);

			tick();
			deq.erase(deq.begin() + deq.size() / 4, deq.begin() + deq.size() / 2);
			deq_t = tock_ms();

			tick();
			vec.erase(vec.begin() + vec.size() / 4, vec.begin() + vec.size() / 2);
			vec_t = tock_ms();

			tick();
			tvec.erase(tvec.begin() + tvec.size() / 4, tvec.begin() + tvec.size() / 2);
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("erase range left side", vec_t, deq_t, tvec_t) ;

			deq.resize(count, 0);
			vec.resize(count, 0);
			tvec.resize(count, 0);

			tick();
			deq.erase(deq.begin() + deq.size() / 2, deq.begin() + deq.size() * 3 / 4);
			deq_t = tock_ms();

			tick();
			vec.erase(vec.begin() + vec.size() / 2, vec.begin() + vec.size() * 3 / 4);
			vec_t = tock_ms();

			tick();
			tvec.erase(tvec.begin() + tvec.size() / 2, tvec.begin() + tvec.size() * 3 / 4);
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("erase range right side", vec_t, deq_t, tvec_t);
		}

		{
			std::vector<type> tmp(count);
			for (size_t i = 0; i < tmp.size(); ++i)
				tmp[i] = i;

			deq.resize(count / 2, 0);
			vec.resize(count / 2, 0);
			tvec.resize(count / 2, 0);

			tick();
			deq.assign(tmp.begin(), tmp.end());
			deq_t = tock_ms();

			tick();
			vec.assign(tmp.begin(), tmp.end());
			vec_t = tock_ms();

			tick();
			tvec.assign(tmp.begin(), tmp.end());
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("assign grow random access", vec_t, deq_t, tvec_t) ;

			deq.resize(count * 2, 0);
			vec.resize(count * 2, 0);
			tvec.resize(count * 2, 0);

			tick();
			deq.assign(tmp.begin(), tmp.end());
			deq_t = tock_ms();

			tick();
			vec.assign(tmp.begin(), tmp.end());
			vec_t = tock_ms();

			tick();
			tvec.assign(tmp.begin(), tmp.end());
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("assign shrink random access", vec_t, deq_t, tvec_t);
		}
		{
			std::list<type> lst;
			for (size_t i = 0; i < count; ++i)
				lst.push_back(i);

			deq.resize(lst.size() / 2, 0);
			vec.resize(lst.size() / 2, 0);
			tvec.resize(lst.size() / 2, 0);

			tick();
			deq.assign(lst.begin(), lst.end());
			deq_t = tock_ms();

			tick();
			vec.assign(lst.begin(), lst.end());
			vec_t = tock_ms();

			tick();
			tvec.assign(lst.begin(), lst.end());
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("assign grow forward iterator", vec_t, deq_t, tvec_t) ;

			deq.resize(lst.size() * 2, 0);
			vec.resize(lst.size() * 2, 0);
			tvec.resize(lst.size() * 2, 0);

			tick();
			deq.assign(lst.begin(), lst.end());
			deq_t = tock_ms();

			tick();
			vec.assign(lst.begin(), lst.end());
			vec_t = tock_ms();

			tick();
			tvec.assign(lst.begin(), lst.end());
			tvec_t = tock_ms();

			assert_equal(deq, tvec);
			write_line("assign shrink forward iterator", vec_t, deq_t, tvec_t);
		}

		deq.resize(count, 0);
		vec.resize(count, 0);
		tvec.resize(count, 0);
		assert_equal(deq, tvec);

		// fill again, backward
		for (size_t i = 0; i < deq.size(); ++i) {
			deq[i] = deq.size() - i - 1;
			vec[i] = vec.size() - i - 1;
			tvec[i] = tvec.size() - i - 1;
		}

		tick();
		while (deq.size() > 25)
			deq.pop_back();
		deq_t = tock_ms();

		tick();
		while (vec.size() > 25)
			vec.pop_back();
		vec_t = tock_ms();

		tick();
		while (tvec.size() > 25)
			tvec.pop_back();
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("pop_back", vec_t, deq_t, tvec_t) ;

		deq.resize(count, 0);
		tvec.resize(count, 0);
		vec.resize(count, 0);
		assert_equal(deq, tvec);

		// fill again, backward
		for (size_t i = 0; i < deq.size(); ++i) {
			deq[i] = deq.size() - i - 1;
			vec[i] = vec.size() - i - 1;
			tvec[i] = tvec.size() - i - 1;
		}

		assert_equal(deq, tvec);

		tick();
		while (deq.size() > count - 10) {
			deq.pop_front();
		}
		deq_t = tock_ms();

		tick();
		while (tvec.size() > count - 10)
			tvec.erase(tvec.begin());
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("pop_front", 1000000000ULL, deq_t, tvec_t) ;

		size_t insert_count = std::min((size_t)50, count );
		std::vector<size_t> in_pos;
		size_t ss = deq.size();
		srand(0);
		for (size_t i = 0; i < insert_count; ++i)
			in_pos.push_back((size_t)rand() % ss++);

		tick();
		for (size_t i = 0; i < insert_count; ++i) {
			deq.insert(deq.begin() + in_pos[i], i);
		}
		deq_t = tock_ms();

		tick();
		for (size_t i = 0; i < insert_count; ++i) {
			tvec.insert(tvec.begin() + in_pos[i], i);
		}
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("insert random position", 1000000000ULL, deq_t, tvec_t) ;

		deq.resize(count, 0);
		tvec.resize(count, 0);

		// fill again, backward
		for (size_t i = 0; i < deq.size(); ++i) {
			deq[i] = deq.size() - i - 1;
			tvec[i] = tvec.size() - i - 1;
		}

		size_t erase_count = std::min((size_t)50, deq.size());
		std::vector<size_t> er_pos;
		size_t sss = count;
		srand(0);
		for (size_t i = 0; i < erase_count; ++i)
			er_pos.push_back((size_t)rand() % sss--);

		tick();
		for (size_t i = 0; i < erase_count; ++i) {
			deq.erase(deq.begin() + er_pos[i]);
		}
		deq_t = tock_ms();

		tick();
		for (size_t i = 0; i < erase_count; ++i) {
			tvec.erase(tvec.begin() + er_pos[i]);
		}
		tvec_t = tock_ms();

		assert_equal(deq, tvec);
		write_line("erase random position", 1000000000ULL, deq_t, tvec_t) ;
	}
}

int bench_cvector(int, char** const)
{

	bench<size_t>(10000000);
	return 0;
}