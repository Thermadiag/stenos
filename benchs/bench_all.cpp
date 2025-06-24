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

#include <fstream>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <sstream>
#include <cstdlib>

#include "benching.hpp"
#include "stenos/stenos.h"
#include "stenos/timer.hpp"
// #ifdef HAS_BLOSC
#include "blosc2.h"
// #endif

/* template<int rshift>
int get_value(int i)
{
	int v = (i << 26) ^ (i << 18) ^ (i << 11) ^ (i << 3) ^ i;
	if (rshift < 32) {
		v &= (1 << rshift) - 1;
	}
	return v;
}
template<int rshift>
static std::vector<int> synthetic(int size)
{
	std::vector<int> res((size_t)size);
	for (int i = 0; i < size; ++i)
		res[(size_t)i] = get_value<rshift>(i);
	return res;
}*/

template<class T>
static size_t compress_vec_stenos(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	size_t res = 0;
	stenos_context* ctx = stenos_make_context();
	stenos_set_level(ctx, level);
	stenos_set_threads(ctx, (int)threads);
	for (int i = 0; i < iterations; ++i)
		res = stenos_compress_generic(ctx, vec.data(), sizeof(typename std::vector<T>::value_type), bytes, dst, dst_size);
	stenos_destroy_context(ctx);
	if (stenos_has_error(res))
		return 0;
	return res;
}

template<class T>
static size_t compress_vec_blosc2_shuffle(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_bitshuffle(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_BITSHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_delta_shuffle(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(1);
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}

/* template<class T>
static size_t compress_vec_blosc2_shuffle_lz4(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	blosc1_set_compressor("lz4");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_bitshuffle_lz4(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	blosc1_set_compressor("lz4");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_BITSHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}*/

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

template<class T>
struct Compress
{
	std::function<size_t(const std::vector<T>&, int, unsigned)> compress;
	std::string name;
	template<class Fun>
	Compress(Fun fun, const std::string& n)
	  : compress(fun)
	  , name(n)
	{
	}
};

template<class T>
void test_compression(const std::vector<T>& vec, const std::vector<Compress<T>>& functions, int level, unsigned threads)
{
	static constexpr size_t width = 20;

	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);

	stenos::timer t;

	// bench
	std::cout << "|";
	for (const auto& c : functions) {
		t.tick();
		auto s = c.compress(vec, level, threads);
		auto el = t.tock() * 1e-6; // ms
		double ratio = (double)bytes / (double)s;
		std::cout << as_aligned_string(width, "%d ms / %.2f", (int)el, ratio) << "|";
	}
	std::cout << std::endl;
}

template<class T>
void test_compression(const std::vector<T>& vec, unsigned threads)
{
	static constexpr size_t width = 20;

	std::vector<char> out(stenos_bound(vec.size() * sizeof(typename std::vector<T>::value_type)));

	std::vector<Compress<T>> functions;
	int iterations = iteration_count(vec);
	functions.push_back(Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_stenos(v, out.data(), out.size(), level, threads, iterations); }, "Stenos"));
	functions.push_back(Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_blosc2_shuffle(v, out.data(), out.size(), level, threads, iterations); }, "Blosc2 shuffle"));
	functions.push_back(
	  Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_blosc2_delta_shuffle(v, out.data(), out.size(), level, threads, iterations); }, "Blosc2 delta"));
	functions.push_back(
	  Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_blosc2_bitshuffle(v, out.data(), out.size(), level, threads, iterations); }, "Blosc2 bitshuffle"));

	// Print info
	std::cout << "Threads: " << threads << ", Iterations: " << iterations << std::endl;

	// print header
	std::cout << "|";
	for (const auto& c : functions)
		std::cout << as_aligned_string(width, "%s", c.name.c_str()) << "|";
	std::cout << std::endl;

	std::cout << "|";
	for (const auto& c : functions)
		std::cout << std::string(width, '-') << "|";
	std::cout << std::endl;

	// print benchmarks
	for (int level = 1; level <= 9; ++level) {
		test_compression(vec, functions, level, threads);
	}
}

template<class T>
void test_time_limited(const std::vector<T>& vec, unsigned threads)
{
	std::vector<char> dst(stenos_bound(vec.size() * sizeof(T)));
	size_t bytes = vec.size() * sizeof(T);

	stenos_timer* timer = stenos_make_timer();

	stenos_tick(timer);
	auto r1 = stenos_compress(vec.data(), sizeof(T), vec.size() * sizeof(T), dst.data(), dst.size(), 8);
	auto el_max = stenos_tock(timer);

	stenos_tick(timer);
	auto r2 = stenos_compress(vec.data(), sizeof(T), vec.size() * sizeof(T), dst.data(), dst.size(), 1);
	auto el_min = stenos_tock(timer);

	std::cout << "ratios: " << (double)bytes / r2 << " to " << (double)bytes / r1 << std::endl;

	// start at half the minimum time
	el_min /= 2;

	while (true) {
		auto ctx = stenos_make_context();
		if (el_min > el_max)
			el_min *= 1000;
		stenos_set_max_nanoseconds(ctx, el_min);

		stenos_tick(timer);
		auto r = stenos_compress_generic(ctx, vec.data(), sizeof(T), vec.size() * sizeof(T), dst.data(), dst.size());
		auto el = stenos_tock(timer);
		std::cout << "request " << el_min << " got " << el << " ratio " << (double)bytes / r << std::endl;
		if (el_min > el_max)
			break;
		el_min *= 2;
	}
}

static unsigned STENOS_THREADS = 1;

template<size_t N, class Type = void>
void bench_file(const char* filename)
{
	std::cout << "Test file " << filename << std::endl;
	if (!std::is_same<Type, void>::value) {
		using type = typename std::conditional<std::is_same<Type, void>::value, int, Type>::type;
		auto vec = read_text<type>(filename);
		test_compression(vec, STENOS_THREADS);
	}
	else {

		auto vec = read_binary<N>(filename);
		test_compression(vec, STENOS_THREADS);
	}
	std::cout << std::endl;
}

int bench_all(int, char** const)
{
	char* _STENOS_THREADS = getenv("STENOS_THREADS");
	if (_STENOS_THREADS) {
		std::istringstream iss(_STENOS_THREADS);
		iss >> STENOS_THREADS;
		if (!iss)
			STENOS_THREADS = 1;
	}

	blosc1_set_compressor("zstd");

	bench_file<12>(STENOS_DATA_DIR "/dataset/12_953134_float3.bin");
	bench_file<16>(STENOS_DATA_DIR "/dataset/16_232630_float4.bin");
	bench_file<16>(STENOS_DATA_DIR "/dataset/16_1024_sq_float4.bin");
	bench_file<16>(STENOS_DATA_DIR "/dataset/16_2048_sq_float4.bin");
	bench_file<2, uint16_t>(STENOS_DATA_DIR "/dataset/2_WA.txt");
	bench_file<2, uint16_t>(STENOS_DATA_DIR "/dataset/2_DIV.txt");
	bench_file<2, uint16_t>(STENOS_DATA_DIR "/dataset/2_LH1.txt");
	bench_file<2>(STENOS_DATA_DIR "/dataset/2_PI240_15s.wav");
	bench_file<8, double>(STENOS_DATA_DIR "/dataset/8_UTOR.txt");
	bench_file<8, double>(STENOS_DATA_DIR "/dataset/8_SHYBPTOT.txt");

	return 0;
}
