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

#include "stenos/cvector.hpp"
#include "stenos/timer.hpp"
#include "testing.hpp"

#include <deque>
#include <list>
#include <vector>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <random>
#include <thread>
#include <sstream>

#ifdef max
#undef min
#undef max
#endif

#define STENOS_TEST(...)                                                                                                                                                                               \
	if (!(__VA_ARGS__))                                                                                                                                                                            \
	STENOS_ABORT("Test error in %s line %i\n", __FILE__, __LINE__)

template<class Alloc, class U>
using RebindAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<U>;

template<class Deq1, class Deq2>
bool equal_cvec(const Deq1& d1, const Deq2& d2)
{
	if (d1.size() != d2.size())
		return false;
	if (d1.size() == 0)
		return true;
	if (d1.front() != d2.front())
		return false;
	if (d1.back() != d2.back()) {
		return false;
	}
	auto it1 = d1.begin();
	auto it2 = d2.begin();
	while (it1 != d1.end()) {
		if (*it1 != *it2) {
			return false;
		}
		++it1;
		++it2;
	}
	return true;
}

template<class Alloc = std::allocator<size_t>>
inline void test_cvector_algorithms(size_t count = 5000000, const Alloc& al = Alloc())
{
	using namespace stenos;

	// Test algorithms on tiered_vector, some of them requiring random access iterators

	typedef size_t type;
	typedef cvector<type, 0, 1, Alloc> cvec_type;

	// Build with non unique random values
	cvec_type cvec(al);
	std::deque<type> deq;
	srand(0); // time(NULL));
	for (size_t i = 0; i < count; ++i) {
		unsigned r = static_cast<unsigned>(rand()); // static_cast<unsigned>(count - i - 1);//rand() & ((1U << 16U) - 1U);
		deq.push_back(static_cast<type>(r));
		cvec.push_back(static_cast<type>(r));
	}

	STENOS_TEST(equal_cvec(deq, cvec));

	// Test sort
	std::sort(deq.begin(), deq.end());
	std::sort(cvec.begin(), cvec.end());

	STENOS_TEST(equal_cvec(deq, cvec));

	// Test unique after sorting
	auto it1 = std::unique(deq.begin(), deq.end());
	auto it2 = std::unique(cvec.begin(), cvec.end());
	deq.resize(static_cast<size_t>(it1 - deq.begin()));
	cvec.resize(static_cast<size_t>(it2 - cvec.begin()));

	STENOS_TEST(equal_cvec(deq, cvec));

	// Reset values
	deq.resize(count);
	cvec.resize(count);

	for (size_t i = 0; i < count; ++i)
		cvec[i] = deq[i] = static_cast<type>(rand());

	// Test rotate
	std::rotate(deq.begin(), deq.begin() + deq.size() / 2, deq.end());
	std::rotate(cvec.begin(), cvec.begin() + cvec.size() / 2, cvec.end());

	STENOS_TEST(equal_cvec(deq, cvec));

	// Test reversing
	std::reverse(deq.begin(), deq.end());
	std::reverse(cvec.begin(), cvec.end());

	STENOS_TEST(equal_cvec(deq, cvec));

	// Reset values
	for (size_t i = 0; i < count; ++i)
		cvec[i] = deq[i] = static_cast<type>(rand());

	// Test partial sort
	std::partial_sort(deq.begin(), deq.begin() + deq.size() / 2, deq.end());
	std::partial_sort(cvec.begin(), cvec.begin() + cvec.size() / 2, cvec.end());
	STENOS_TEST(equal_cvec(deq, cvec));

	for (size_t i = 0; i < count; ++i)
		cvec[i] = deq[i] = static_cast<type>(rand());

	// Strangely, msvc implementation of std::nth_element produce a warning as it tries to modify the value of const iterator
	// Test nth_element
	std::nth_element(deq.begin(), deq.begin() + deq.size() / 2, deq.end());
	std::nth_element(cvec.begin(), cvec.begin() + cvec.size() / 2, cvec.end());
	STENOS_TEST(equal_cvec(deq, cvec));
}

template<class Alloc>
inline void test_cvector_move_only(size_t count, const Alloc& al = Alloc())
{
	using namespace stenos;
	using Al = RebindAlloc<Alloc, std::unique_ptr<size_t>>;

	typedef cvector<std::unique_ptr<size_t>, 0, 1, Al> cvec_type;

	std::deque<std::unique_ptr<size_t>> deq;
	cvec_type cvec(al);

	srand(0);
	for (size_t i = 0; i < count; ++i) {
		unsigned r = static_cast<unsigned>(rand());
		deq.emplace_back(new size_t(r));
		cvec.emplace_back(new size_t(r));
	}

	for (size_t i = 0; i < count; ++i) {
		STENOS_TEST(*deq[i] == *cvec[i].get());
	}

	auto less = [](const std::unique_ptr<size_t>& a, const std::unique_ptr<size_t>& b) { return *a < *b; };
	std::sort(deq.begin(), deq.end(), less);
	std::sort(cvec.begin(), cvec.end(), less);

	for (size_t i = 0; i < count; ++i) {
		STENOS_TEST(*deq[i] == *cvec[i].get());
	}

	// test std::move and std::move_backward
	std::deque<std::unique_ptr<size_t>> deq2(deq.size());
	cvec_type cvec2(cvec.size(), al);

	std::move(deq.begin(), deq.end(), deq2.begin());
	std::move(cvec.begin(), cvec.end(), cvec2.begin());

	for (size_t i = 0; i < count; ++i) {
		STENOS_TEST(deq[i].get() == NULL);
		STENOS_TEST(cvec[i].get().get() == NULL);
		STENOS_TEST(*deq2[i] == *cvec2[i].get());
	}

	std::move_backward(deq2.begin(), deq2.end(), deq.end());
	std::move_backward(cvec2.begin(), cvec2.end(), cvec.end());

	for (size_t i = 0; i < count; ++i) {
		STENOS_TEST(deq2[i].get() == NULL);
		STENOS_TEST(cvec2[i].get().get() == NULL);
		STENOS_TEST(*deq[i] == *cvec[i].get());
	}

	deq.resize(deq.size() / 2);
	cvec.resize(cvec.size() / 2);
	STENOS_TEST(std::equal(deq.begin(), deq.end(), cvec.begin(), cvec.end(), ([](const std::unique_ptr<size_t>& a, const std::unique_ptr<size_t>& b) { return *a == *b; })));

	deq.resize(deq.size() * 2);
	cvec.resize(cvec.size() * 2);
	STENOS_TEST(std::equal(deq.begin(), deq.end(), cvec.begin(), cvec.end(), ([](const std::unique_ptr<size_t>& a, const std::unique_ptr<size_t>& b) { return a == b || *a == *b; })));
}

template<class Alloc, class U>
using RebindAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<U>;

template<class T, class Alloc = std::allocator<T>>
void test_cvector(size_t count = 5000000, const Alloc al = Alloc())
{
	using namespace stenos;

	// First, test some stl algorithms
	// std::cout << "Test cvector algorithms..." << std::endl;
	test_cvector_algorithms(count, al);
	// std::cout << "Test cvector move only..." << std::endl;
	test_cvector_move_only(count, al);

	typedef T type;
	std::deque<type> deq;
	typedef cvector<type, 0, 1, Alloc> cvec_type;
	cvec_type cvec(al);
	std::vector<type> vec;

	STENOS_TEST(cvec.begin() == cvec.end());
	STENOS_TEST(cvec.size() == 0);

	cvec.resize(10);
	STENOS_TEST(cvec.size() == 10);
	cvec.clear();
	STENOS_TEST(cvec.size() == 0);

	// Fill containers
	for (size_t i = 0; i < count; ++i)
		deq.push_back(static_cast<T>(i));
	for (size_t i = 0; i < count; ++i)
		cvec.push_back(static_cast<T>(i));
	for (size_t i = 0; i < count; ++i)
		vec.push_back(static_cast<T>(i));

	STENOS_TEST(equal_cvec(deq, cvec));

	// Test resize lower
	deq.resize(deq.size() / 10);
	cvec.resize(cvec.size() / 10);
	STENOS_TEST(equal_cvec(deq, cvec));

	// Test resize upper
	deq.resize(count, 0);
	cvec.resize(count, 0);
	STENOS_TEST(equal_cvec(deq, cvec));

	{
		// Test copy contruct
		std::deque<type> d2 = deq;
		cvec_type dd2(cvec, al);
		STENOS_TEST(equal_cvec(d2, dd2));
	}

	{
		// Test insert range based on random access itertor, left side
		deq.insert(deq.begin() + (deq.size() * 2) / 5, vec.begin(), vec.end());
		cvec.insert(cvec.begin() + (cvec.size() * 2) / 5, vec.begin(), vec.end());
		STENOS_TEST(equal_cvec(deq, cvec));

		deq.resize(count);
		cvec.resize(count);
		STENOS_TEST(equal_cvec(deq, cvec));

		// Test insert range based on random access itertor, right side
		deq.insert(deq.begin() + (deq.size() * 3) / 5, vec.begin(), vec.end());
		cvec.insert(cvec.begin() + (cvec.size() * 3) / 5, vec.begin(), vec.end());

		STENOS_TEST(equal_cvec(deq, cvec));

		deq.resize(count);
		cvec.resize(count);

		STENOS_TEST(equal_cvec(deq, cvec));
	}

	{
		// Rest values
		for (size_t i = 0; i < deq.size(); ++i) {
			deq[i] = cvec[i] = static_cast<T>(i);
		}
		STENOS_TEST(equal_cvec(deq, cvec));

		// Test erase range, left side
		deq.erase(deq.begin() + deq.size() / 4, deq.begin() + deq.size() / 2);
		cvec.erase(cvec.begin() + cvec.size() / 4, cvec.begin() + cvec.size() / 2);
		STENOS_TEST(equal_cvec(deq, cvec));

		deq.resize(count, 0);
		cvec.resize(count, 0);

		// Test erase range, right side
		deq.erase(deq.begin() + deq.size() / 2, deq.begin() + deq.size() * 3 / 4);
		cvec.erase(cvec.begin() + cvec.size() / 2, cvec.begin() + cvec.size() * 3 / 4);
		STENOS_TEST(equal_cvec(deq, cvec));
	}

	{
		deq.resize(vec.size() / 2, 0);
		cvec.resize(vec.size() / 2, 0);

		// Test assign from lower size
		deq.assign(vec.begin(), vec.end());
		cvec.assign(vec.begin(), vec.end());
		STENOS_TEST(equal_cvec(deq, cvec));

		deq.resize(vec.size() * 2, 0);
		cvec.resize(vec.size() * 2, 0);

		// Test assign from greater size
		deq.assign(vec.begin(), vec.end());
		cvec.assign(vec.begin(), vec.end());
		STENOS_TEST(equal_cvec(deq, cvec));
	}
	{
		std::list<type> lst;
		for (size_t i = 0; i < count; ++i)
			lst.push_back(static_cast<T>(i));

		deq.resize(lst.size() / 2, 0);
		cvec.resize(lst.size() / 2, 0);

		// Test assign from forward iterators
		deq.assign(lst.begin(), lst.end());
		cvec.assign(lst.begin(), lst.end());
		STENOS_TEST(equal_cvec(deq, cvec));

		deq.resize(lst.size() * 2, 0);
		cvec.resize(lst.size() * 2, 0);

		// Test assign from forward iterators
		deq.assign(lst.begin(), lst.end());
		cvec.assign(lst.begin(), lst.end());
		STENOS_TEST(equal_cvec(deq, cvec));
	}

	deq.resize(count, 0);
	cvec.resize(count, 0);
	STENOS_TEST(equal_cvec(deq, cvec));

	// fill again, backward
	for (size_t i = 0; i < deq.size(); ++i) {
		deq[i] = static_cast<T>(deq.size() - i - 1);
		cvec[i] = static_cast<T>(cvec.size() - i - 1);
	}

	// Test pop_back
	while (deq.size() > 25)
		deq.pop_back();
	while (cvec.size() > 25)
		cvec.pop_back();
	STENOS_TEST(equal_cvec(deq, cvec));

	deq.resize(count, 0);
	cvec.resize(count, 0);

	STENOS_TEST(equal_cvec(deq, cvec));

	// fill again, backward
	for (size_t i = 0; i < deq.size(); ++i) {
		deq[i] = static_cast<T>(deq.size() - i - 1);
		cvec[i] = static_cast<T>(cvec.size() - i - 1);
	}

	STENOS_TEST(equal_cvec(deq, cvec));

	size_t stop = static_cast<size_t>(static_cast<double>(deq.size()) * 0.9);
	// Test pop_front
	while (deq.size() > stop) {
		deq.pop_front();
	}
	while (cvec.size() > stop)
		cvec.erase(cvec.begin());
	STENOS_TEST(equal_cvec(deq, cvec));

	{
		// Test insert/erase single element
		cvec_type d(al);
		std::deque<type> dd;
		d.resize(128 * 3, 0);
		dd.resize(128 * 3, 0);
		for (size_t i = 0; i < d.size(); ++i) {
			d[i] = dd[i] = static_cast<T>(i);
		}
		STENOS_TEST(equal_cvec(d, dd));
		d.insert(d.begin() + 10, static_cast<type>(-1));
		dd.insert(dd.begin() + 10, static_cast<type>(-1));
		STENOS_TEST(equal_cvec(d, dd));
		for (size_t i = 0; i < 128; ++i) {
			d.erase(d.begin());
			dd.erase(dd.begin());
			STENOS_TEST(equal_cvec(d, dd));
		}
		STENOS_TEST(equal_cvec(d, dd));
		d.erase(d.begin());
		dd.erase(dd.begin());
		STENOS_TEST(equal_cvec(d, dd));
	}

	unsigned insert_count = static_cast<unsigned>(std::max(static_cast<size_t>(50), count / 50));
	std::vector<std::ptrdiff_t> in_pos;
	int ss = static_cast<int>(deq.size());
	srand(0);
	for (unsigned i = 0; i < insert_count; ++i)
		in_pos.push_back(rand() % ss++);

	// Test insert single value at random position
	for (unsigned i = 0; i < insert_count; ++i) {
		deq.insert(deq.begin() + in_pos[i], static_cast<T>(i));
	}
	for (unsigned i = 0; i < insert_count; ++i) {
		cvec.insert(cvec.begin() + in_pos[i], static_cast<T>(i));
	}
	STENOS_TEST(equal_cvec(deq, cvec));

	{
		// Test erase single value at random position
		cvec_type d(al);
		std::deque<type> dd;
		d.resize(100, 0);
		dd.resize(100, 0);
		for (size_t i = 0; i < d.size(); ++i) {
			d[i] = dd[i] = static_cast<T>(i);
		}

		for (int i = 0; i < 50; ++i) {
			int pos = i % 5;
			pos = static_cast<int>(d.size()) * pos / 4;
			if (pos == static_cast<int>(d.size()))
				--pos;
			dd.erase(dd.begin() + pos);
			d.erase(d.begin() + pos);
			STENOS_TEST(equal_cvec(d, dd));
		}
	}

	deq.resize(count, 0);
	cvec.resize(count, 0);

	// Test shrink_to_fit
	deq.shrink_to_fit();
	cvec.shrink_to_fit();
	STENOS_TEST(equal_cvec(deq, cvec));

	// fill again, backward
	for (size_t i = 0; i < deq.size(); ++i) {
		deq[i] = static_cast<T>(deq.size() - i - 1);
		cvec[i] = static_cast<T>(cvec.size() - i - 1);
	}

	// Test erase single values at random position
	size_t erase_count = deq.size() / 8;
	std::vector<std::ptrdiff_t> er_pos;
	size_t sss = count;
	srand(0);
	for (size_t i = 0; i < erase_count; ++i)
		er_pos.push_back(rand() % static_cast<int>(sss--));

	for (size_t i = 0; i < erase_count; ++i) {
		deq.erase(deq.begin() + er_pos[i]);
	}
	for (size_t i = 0; i < erase_count; ++i) {
		cvec.erase(cvec.begin() + er_pos[i]);
	}
	STENOS_TEST(equal_cvec(deq, cvec));

	cvec.resize(count);
	deq.resize(count);
	for (size_t i = 0; i < deq.size(); ++i) {
		deq[i] = cvec[i] = static_cast<T>(i);
	}

	// Test move assign and move copy

	std::deque<type> deq2 = std::move(deq);
	cvec_type tvec2(std::move(cvec), al);
	STENOS_TEST(equal_cvec(deq2, tvec2) && tvec2.size() > 0 && deq.size() == 0 && cvec.size() == 0);

	deq = std::move(deq2);
	cvec = std::move(tvec2);
	STENOS_TEST(equal_cvec(deq, cvec) && cvec.size() > 0 && tvec2.size() == 0 && deq2.size() == 0);
}

template<class T>
void copy_to_cvector(const stenos::cvector<T>& in, stenos::cvector<T>& out)
{
	out.resize(in.size());
	std::copy(in.begin(), in.end(), out.begin());
}
template<class T>
void copy_to_vector(const stenos::cvector<T>& in, std::vector<T>& out)
{
	out.resize(in.size());
	std::copy(in.begin(), in.end(), out.begin());
}

static void test_serialize()
{
	// Test serialize/deserialize
	CountAlloc<size_t> al;

	{
		using vector_type = stenos::cvector<size_t, 0, 1, CountAlloc<size_t>>;
		vector_type v(al);
		for (size_t i = 0; i < 1000000; ++i)
			v.emplace_back(i);
		std::mt19937 rng(0);
		std::shuffle(v.begin(), v.end(), rng);

		std::ostringstream oss;
		v.serialize(oss);

		std::string str = oss.str();
		std::istringstream iss(str);

		vector_type v2(al);
		v2.deserialize(iss);

		STENOS_TEST(equal_cvec(v, v2));

		std::vector<size_t> v3(v.size());
		size_t r = stenos_decompress(str.data(), sizeof(size_t), str.size(), v3.data(), v3.size() * sizeof(size_t));
		STENOS_TEST(r == v3.size() * sizeof(size_t));
		STENOS_TEST(equal_cvec(v3, v2));
	}
	STENOS_TEST(get_alloc_bytes(al) == 0);

	// Same with the buffer interface
	{
		using vector_type = stenos::cvector<size_t, 0, 1, CountAlloc<size_t>>;
		vector_type v(al);
		for (size_t i = 0; i < 1000000; ++i)
			v.emplace_back(i);
		std::mt19937 rng(0);
		std::shuffle(v.begin(), v.end(), rng);

		std::string str(stenos_bound(v.size() * sizeof(size_t)), (char)0);
		size_t c = v.serialize((char*)str.data(), str.size());
		str.resize(c);

		vector_type v2(al);
		v2.deserialize(str.data(), str.size());

		STENOS_TEST(equal_cvec(v, v2));

		std::vector<size_t> v3(v.size());
		size_t r = stenos_decompress(str.data(), sizeof(size_t), str.size(), v3.data(), v3.size() * sizeof(size_t));
		STENOS_TEST(r == v3.size() * sizeof(size_t));
		STENOS_TEST(equal_cvec(v3, v2));
	}
	STENOS_TEST(get_alloc_bytes(al) == 0);
}

static void test_for_each()
{
	CountAlloc<size_t> al;
	{
		stenos::cvector<int, 0, 1, CountAlloc<size_t>> v(al);
		v.resize(999999, 0);

		// Basic testing through the whole vector
		v.for_each(0, v.size(), [](int& i) { ++i; });

		int count = 0;
		v.const_for_each(0, v.size(), [&](int i) { count += i; });
		STENOS_TEST((size_t)count == v.size());

		v.for_each_backward(0, v.size(), [](int& i) { ++i; });

		count = 0;
		v.const_for_each_backward(0, v.size(), [&](int i) { count += i; });
		STENOS_TEST((size_t)count == v.size() * 2);

		std::fill_n(v.begin(), v.size(), 0);

		// Half the vector
		v.for_each(0, v.size() / 2, [](int& i) { ++i; });

		count = 0;
		v.const_for_each(0, v.size() / 2, [&](int i) { count += i; });
		STENOS_TEST((size_t)count == v.size() / 2);

		v.for_each_backward(v.size() / 2, v.size(), [](int& i) { i = 1; });

		count = 0;
		v.const_for_each_backward(v.size() / 2, v.size(), [&](int i) { count += i; });
		STENOS_TEST((size_t)count == v.size() - v.size() / 2);

		// Test early stop
		v.clear();
		for (int i = 0; i < 999999; ++i)
			v.push_back(i);

		size_t walk = v.for_each(0, v.size(), [](int i) { return i < 5000; });
		STENOS_TEST(walk == 5000);
		walk = v.const_for_each(0, v.size(), [](int i) { return i < 5000; });
		STENOS_TEST(walk == 5000);

		walk = v.for_each_backward(0, v.size(), [](int i) { return i > 5000; });
		STENOS_TEST(walk == v.size() - 5001);

		walk = v.const_for_each_backward(0, v.size(), [](int i) { return i > 5000; });
		STENOS_TEST(walk == v.size() - 5001);

		// Test no walk at all
		STENOS_TEST(v.for_each(0, 0, [](int i) { return true; }) == 0);
		STENOS_TEST(v.for_each(0, v.size(), [](int i) { return false; }) == 0);
		STENOS_TEST(v.for_each_backward(0, 0, [](int i) { return true; }) == 0);
		STENOS_TEST(v.for_each_backward(0, v.size(), [](int i) { return false; }) == 0);
	}
	STENOS_TEST(get_alloc_bytes(al) == 0);
}

static inline void test_copy()
{
	{
		stenos::cvector<int> vec;
		for (size_t i = 0; i < vec.size(); ++i)
			vec.push_back(static_cast<int>(i));

		stenos::cvector<int> out1;
		std::vector<int> out2;
		copy_to_cvector(vec, out1);
		copy_to_vector(vec, out2);
	}
}

static size_t Test_count = 0;
struct Test
{
	size_t value;

	Test(size_t v = 0)
	  : value(v)
	{
		++Test_count;
	}
	Test(const Test& o)
	  : value(o.value)
	{
		++Test_count;
	}
	~Test() { --Test_count; }
	Test& operator=(const Test& o)
	{
		value = o.value;
		return *this;
	}

	operator size_t() const { return value; }
};

inline bool operator==(const Test& l, const Test& r)
{
	return l.value == r.value;
}
inline bool operator!=(const Test& l, const Test& r)
{
	return l.value != r.value;
}
inline bool operator<(const Test& l, const Test& r)
{
	return l.value < r.value;
}

template<>
struct stenos::is_relocatable<Test> : std::true_type
{
};

int test_cvector(int, char*[])
{

	using namespace stenos;

	test_copy();
	test_for_each();
	test_serialize();

	{
		CountAlloc<std::atomic<int>> al;
		{
			// Test concurrent access in read mode
			cvector<std::atomic<int>, 0, 1, CountAlloc<std::atomic<int>>> v(al);
			v.resize(500000);
			for (auto& i : v)
				i.move().store(0);

			int count = 100;

			std::thread ths[16];
			std::atomic<bool> start{ false };
			for (int t = 0; t < 16; ++t) {
				ths[t] = std::thread([&]() {
					while (!start.load())
						std::this_thread::yield();
					for (int i = 0; i < count; ++i) {

						for (auto& i : v)
							i.move().fetch_add(1);
					}
				});
			}

			start.store(true);
			for (int t = 0; t < 16; ++t)
				ths[t].join();

			v.shrink_to_fit();

			std::cout << "fetch_add " << v.current_compression_ratio() << std::endl;
			for (auto& i : v) {
				STENOS_TEST(i.get().load() == count * 16);
			}
		}
		STENOS_TEST(get_alloc_bytes(al) == 0);
	}

	CountAlloc<size_t> al;
	// Test cvector and potential memory leak or wrong allocator propagation
	test_cvector<size_t>(50000, al);
	STENOS_TEST(get_alloc_bytes(al) == 0);

	CountAlloc<Test> al2;
	test_cvector<Test>(50000, al2);
	STENOS_TEST(get_alloc_bytes(al2) == 0);
	STENOS_TEST(Test_count == 0);

	return 0;
}
