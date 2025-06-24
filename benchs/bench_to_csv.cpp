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

#include <cstdlib>
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

#include "benching.hpp"
#include "stenos/stenos.h"
#include "stenos/timer.hpp"
// #ifdef HAS_BLOSC
#include "blosc2.h"
// #endif

static unsigned STENOS_THREADS = 1;

template<class T>
static size_t compress_vec_blosc2_shuffle_zstd(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc1_set_compressor("zstd");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_bitshuffle_zstd(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc1_set_compressor("zstd");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_BITSHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}

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
std::string test_to_csv(const std::vector<T>& vec)
{
	std::ostringstream oss;
	oss.imbue(std::locale("de_DE.utf8"));

	
	oss << "Level;StenosSpeed;StenosRatio;BloscZstdShuffleSpeed;BloscZstdShuffleRatio;BloscZstdBitshuffleSpeed;BloscZstdBitshuffleRatio" << std::endl;

	int iterations = iteration_count(vec);

	std::vector<char> out(stenos_bound(vec.size() * sizeof(T)));

	std::vector<std::function<size_t(int)>> funs;
	funs.push_back([&](int level) { return compress_vec_stenos(vec, out.data(), out.size(), level, STENOS_THREADS, iterations); });
	funs.push_back([&](int level) { return compress_vec_blosc2_shuffle_zstd(vec, out.data(), out.size(), level, STENOS_THREADS, iterations); });
	funs.push_back([&](int level) { return compress_vec_blosc2_bitshuffle_zstd(vec, out.data(), out.size(), level, STENOS_THREADS, iterations); });

	auto timer = stenos_make_timer();

	for (int level = 1; level <= 9; ++level) {

		oss << level << ";";
		for (size_t i = 0; i < funs.size(); ++i) {

			double min_el = std::numeric_limits<double>::infinity();
			size_t r = 0;

			// Run the benchmark 5 times and take the best one
			for (int j = 0; j < 5; ++j) {
				stenos_tick(timer);
				r = funs[i](level);
				auto el = stenos_tock(timer) * 1e-9;
				min_el = std::min(el, min_el);
			}

			double ratio = (double)(vec.size() * sizeof(T)) / r;
			double speed = (double)(vec.size() * sizeof(T)) / (min_el / iterations);

			oss << speed << ";" << ratio << ";";
		}
		oss << std::endl;
	}

	stenos_destroy_timer(timer);

	return oss.str();
}


template<size_t N, class Type = void>
void bench_file_csv(const char* filename, std::ofstream &csv)
{
	std::cout << "Test file " << filename << std::endl;
	std::string res;
	if (!std::is_same<Type, void>::value) {
		using type = typename std::conditional<std::is_same<Type, void>::value, int, Type>::type;
		auto vec = read_text<type>(filename);
		res = test_to_csv(vec);
	}
	else {

		auto vec = read_binary<N>(filename);
		res = test_to_csv(vec);
	}
	std::cout << res << std::endl;

	csv << file_name(filename) << std::endl;
	csv << res << std::endl;
	csv << std::endl;

	std::cout << std::endl;
}


int bench_to_csv(int, char** const)
{
	char* _STENOS_THREADS = getenv("STENOS_THREADS");
	if (_STENOS_THREADS) {
		std::istringstream iss(_STENOS_THREADS);
		iss >> STENOS_THREADS;
		if (!iss)
			STENOS_THREADS = 1;
	}

	std::ofstream csv("results.csv");
	csv << "sep=;" << std::endl;

	/* bench_file_csv<12>(STENOS_DATA_DIR "/dataset/12_953134_float3.bin", csv);
	bench_file_csv<16>(STENOS_DATA_DIR "/dataset/16_232630_float4.bin", csv);
	bench_file_csv<16>(STENOS_DATA_DIR "/dataset/16_1024_sq_float4.bin", csv);
	bench_file_csv<16>(STENOS_DATA_DIR "/dataset/16_2048_sq_float4.bin", csv);
	bench_file_csv<2, uint16_t>(STENOS_DATA_DIR "/dataset/2_WA.txt", csv);
	bench_file_csv<2, uint16_t>(STENOS_DATA_DIR "/dataset/2_DIV.txt", csv);
	bench_file_csv<2, uint16_t>(STENOS_DATA_DIR "/dataset/2_LH1.txt", csv);
	bench_file_csv<2>(STENOS_DATA_DIR "/dataset/2_PI240_15s.wav", csv);
	bench_file_csv<8, double>(STENOS_DATA_DIR "/dataset/8_UTOR.txt", csv);*/
	bench_file_csv<8, double>(STENOS_DATA_DIR "/dataset/8_SHYBPTOT.txt", csv);
	
	return 0;
}