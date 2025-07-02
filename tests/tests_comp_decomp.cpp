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

#include "stenos/stenos.h"
#include "stenos/bits.hpp"

#include <vector>
#include <random>
#include <algorithm>
#include <array>

#define TEST(cond)                                                                                                                                                                                     \
	if (!(cond))                                                                                                                                                                                   \
		STENOS_ABORT("Test error in %s line %i distribution %s level %i threads %i dst_size %i\n", __FILE__, __LINE__, distribution, level, threads, (int)dst_size);

template<class T>
std::vector<T> generate_random(size_t size)
{
	std::mt19937 rng(0);
	std::uniform_int_distribution<uint16_t> dist(0, 255);

	std::vector<T> res(size);
	for (size_t i = 0; i < size; ++i) {
		uint8_t* data = (uint8_t*)&res[i];
		for (size_t j = 0; j < sizeof(T); ++j)
			data[j] = (uint8_t)dist(rng);
	}
	return res;
}

template<class T, size_t Start, size_t End>
std::vector<T> generate_random_subpart(size_t size)
{
	std::mt19937 rng(0);
	std::uniform_int_distribution<uint16_t> dist(0, 255);

	std::vector<T> res(size);
	memset(res.data(), 0, res.size() * sizeof(T));
	for (size_t i = 0; i < size; ++i) {
		uint8_t* data = (uint8_t*)&res[i];
		for (size_t j = Start; j < End; ++j)
			data[j] = (uint8_t)dist(rng);
	}
	return res;
}

template<class T>
std::vector<T> generate_random_sorted(size_t size)
{
	auto res = generate_random<T>(size);
	std::sort(res.begin(), res.end());
	return res;
}

template<class T>
std::vector<T> generate_same(size_t size)
{
	std::mt19937 rng(0);
	std::uniform_int_distribution<uint16_t> dist(0, 255);

	std::vector<T> res(size);
	memset(res.data(), (int)dist(rng), res.size() * sizeof(T));
	return res;
}

template<class T>
void test_vector(const std::vector<T>& vec, const char* distribution, int level, int threads, size_t dst_size)
{
	size_t bytesoftype = sizeof(T);
	size_t bytes = vec.size() * bytesoftype;

	std::vector<char> dst(dst_size + 1024);
	uint64_t sentinel = 12345678912345;
	for (size_t i = 0; i < 1024 / 8; ++i)
		stenos::write_LE_64(dst.data() + dst_size + i * 8, sentinel);

	auto ctx = stenos_make_context();
	stenos_set_level(ctx, level);
	stenos_set_threads(ctx, threads); // TEST
	auto r = stenos_compress_generic(ctx, vec.data(), bytesoftype, bytes, dst.data(), dst_size);

	// Test no write beyong output end
	for (size_t i = 0; i < 1024 / 8; ++i) {

		uint64_t new_sentinel = stenos::read_LE_64(dst.data() + dst_size + i * 8);

		// DEBUG
		if (new_sentinel != sentinel)
			stenos_compress_generic(ctx, vec.data(), bytesoftype, bytes, dst.data(), dst_size);

		TEST(new_sentinel == sentinel);
	}

	if (stenos_has_error(r)) {
		if (dst_size >= stenos_bound(bytes))
			stenos_compress_generic(ctx, vec.data(), bytesoftype, bytes, dst.data(), dst_size); // TEST
		TEST(dst_size < stenos_bound(bytes));
		stenos_destroy_context(ctx);
		return;
	}

	std::vector<char> vec2(bytes + 1024);
	for (size_t i = 0; i < 1024 / 8; ++i)
		stenos::write_LE_64(vec2.data() + bytes + i * 8, sentinel);

	// Test decompression error
	auto r2 = stenos_decompress_generic(ctx, dst.data(), bytesoftype, r, vec2.data(), vec2.size());

	// DEBUG
	if (!(!stenos_has_error(r2) && r2 == bytes))
		stenos_decompress_generic(ctx, dst.data(), bytesoftype, r, vec2.data(), vec2.size());

	TEST(!stenos_has_error(r2) && r2 == bytes);

	// Test decompressed buffer equal to input
	int cmp = memcmp(vec.data(), vec2.data(), bytes);
	TEST(cmp == 0);

	// Test no write beyong output end
	for (size_t i = 0; i < 1024 / 8; ++i) {

		uint64_t new_sentinel = stenos::read_LE_64(vec2.data() + bytes + i * 8);

		// DEBUG
		if (new_sentinel != sentinel)
			stenos_decompress_generic(ctx, dst.data(), bytesoftype, r, vec2.data(), vec2.size());

		TEST(new_sentinel == sentinel);
	}

	stenos_destroy_context(ctx);
}

template<class T>
void test_distribution(const char* distribution, const std::vector<T>& vec)
{
	size_t bytes = vec.size() * sizeof(T);

	std::mt19937 rng(0);
	std::uniform_int_distribution<int> dist(0, (int)(bytes > 100 ? bytes / 10 : 10));

	for (int threads = 1; threads <= 8; ++threads) {
		for (int level = 0; level <= 5; ++level) {

			int dst_size = (int)stenos_bound(bytes);
			for (;;) {
				printf("Test %s BPP %i with level %i, %i threads, %i dst_size...", distribution, (int)sizeof(T), level, threads, (int)dst_size);
				test_vector(vec, distribution, level, threads, dst_size);
				printf("done\n");
				if (dst_size == 0)
					break;
				dst_size -= dist(rng);
				if (dst_size < 0)
					dst_size = 0;
			}
		}
	}
}

template<size_t Start, size_t End>
struct TestDistribution
{
	static void apply(const char* distribution)
	{
		using type = std::array<char, Start>;
		using vector_type = std::vector<type>;

		std::mt19937 rng(0);
		std::uniform_int_distribution<int> dist(0, 100000);

		// Test for different input size
		for (int i = 0; i < 1000000; i += dist(rng)) {
			printf("Start test %s for size %i\n", distribution, i);

			vector_type vec;
			if (strcmp("random", distribution) == 0)
				vec = generate_random<type>((size_t)i);
			else if (strcmp("sorted", distribution) == 0)
				vec = generate_random_sorted<type>((size_t)i);
			else if (strcmp("same", distribution) == 0)
				vec = generate_same<type>((size_t)i);
			else
				STENOS_ABORT("Unknown distribution %s", distribution);

			test_distribution(distribution, vec);
		}

		TestDistribution<Start + 1, End>::apply(distribution);
	}
};
template<size_t Start>
struct TestDistribution<Start, Start>
{
	static void apply(const char*) {}
};

int tests_comp_decomp(int, char*[])
{

	TestDistribution<1, 16>::apply("same");
	TestDistribution<1, 16>::apply("sorted");
	TestDistribution<1, 16>::apply("random");

	return 0;
}
