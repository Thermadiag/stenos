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

#include "delta.h"
#include "simd.h"

namespace stenos
{
	static inline void delta_generic(const void* _src, void* _dst, size_t bytes)
	{
		// Generic byte delta implementation.
		// Byte delta is applied independantly on 4 streams
		// to fasten the inverse operation.

		const char* src = (const char*)_src;
		char* dst = (char*)_dst;

		if (bytes == 0)
			return;

		if (bytes <= 2048) {

			*dst++ = *src++;
			--bytes;
			for (size_t i = 0; i < bytes; ++i)
				dst[i] = src[i] - src[i - 1];
		}
		else {
			size_t bytes4 = bytes / 4;
			const char* s[4] = { src, src + bytes4, src + bytes4 * 2, src + bytes4 * 3 };
			char* d[4] = { dst, dst + bytes4, dst + bytes4 * 2, dst + bytes4 * 3 };

			*d[0]++ = *s[0]++;
			*d[1]++ = *s[1]++;
			*d[2]++ = *s[2]++;
			*d[3]++ = *s[3]++;

			--bytes4;
			for (size_t i = 0; i < bytes4; ++i) {
				d[0][i] = s[0][i] - s[0][i - 1];
				d[1][i] = s[1][i] - s[1][i - 1];
				d[2][i] = s[2][i] - s[2][i - 1];
				d[3][i] = s[3][i] - s[3][i - 1];
			}

			size_t start = (bytes4 + 1) * 4;
			for (; start != bytes; ++start)
				dst[start] = src[start] - src[start - 1];
		}
	}

#ifdef __SSE2__

	static inline void delta_sse2(const void* _src, void* _dst, size_t bytes)
	{
		// SSE2 straightforward implementation

		const char* src = (const char*)_src;
		char* dst = (char*)_dst;

		if (bytes == 0)
			return;

		if (bytes <= 2048) {

			*dst++ = *src++;
			--bytes;
			size_t size16 = bytes & ~(15ull);
			for (size_t i = 0; i < size16; i += 16) {
				__m128i in1 = _mm_loadu_si128((const __m128i*)(src + i));
				__m128i in2 = _mm_loadu_si128((const __m128i*)(src + i - 1));
				_mm_storeu_si128((__m128i*)(dst + i), _mm_sub_epi8(in1, in2));
			}
			for (size_t i = size16; i < bytes; ++i)
				dst[i] = src[i] - src[i - 1];
		}
		else {
			size_t bytes4 = bytes / 4;
			const char* s[4] = { src, src + bytes4, src + bytes4 * 2, src + bytes4 * 3 };
			char* d[4] = { dst, dst + bytes4, dst + bytes4 * 2, dst + bytes4 * 3 };

			*d[0]++ = *s[0]++;
			*d[1]++ = *s[1]++;
			*d[2]++ = *s[2]++;
			*d[3]++ = *s[3]++;

			--bytes4;
			size_t size16 = bytes4 & ~(15ull);
			for (size_t i = 0; i < size16; i += 16) {
				__m128i in1 = _mm_loadu_si128((const __m128i*)(s[0] + i));
				__m128i in2 = _mm_loadu_si128((const __m128i*)(s[0] + i - 1));
				_mm_storeu_si128((__m128i*)(d[0] + i), _mm_sub_epi8(in1, in2));

				in1 = _mm_loadu_si128((const __m128i*)(s[1] + i));
				in2 = _mm_loadu_si128((const __m128i*)(s[1] + i - 1));
				_mm_storeu_si128((__m128i*)(d[1] + i), _mm_sub_epi8(in1, in2));

				in1 = _mm_loadu_si128((const __m128i*)(s[2] + i));
				in2 = _mm_loadu_si128((const __m128i*)(s[2] + i - 1));
				_mm_storeu_si128((__m128i*)(d[2] + i), _mm_sub_epi8(in1, in2));

				in1 = _mm_loadu_si128((const __m128i*)(s[3] + i));
				in2 = _mm_loadu_si128((const __m128i*)(s[3] + i - 1));
				_mm_storeu_si128((__m128i*)(d[3] + i), _mm_sub_epi8(in1, in2));
			}
			for (size_t i = size16; i < bytes4; ++i) {
				d[0][i] = s[0][i] - s[0][i - 1];
				d[1][i] = s[1][i] - s[1][i - 1];
				d[2][i] = s[2][i] - s[2][i - 1];
				d[3][i] = s[3][i] - s[3][i - 1];
			}

			size_t start = (bytes4 + 1) * 4;
			for (; start != bytes; ++start)
				dst[start] = src[start] - src[start - 1];
		}
	}

#endif

#ifdef __AVX2__

	static inline void delta_avx2(const void* _src, void* _dst, size_t bytes)
	{
		// AVX2 straightforward implementation

		const char* src = (const char*)_src;
		char* dst = (char*)_dst;

		if (bytes == 0)
			return;

		if (bytes <= 2048) {

			*dst++ = *src++;
			--bytes;
			size_t size16 = bytes & ~(31ull);
			for (size_t i = 0; i < size16; i += 32) {
				__m256i in1 = _mm256_loadu_si256((const __m256i*)(src + i));
				__m256i in2 = _mm256_loadu_si256((const __m256i*)(src + i - 1));
				_mm256_storeu_si256((__m256i*)(dst + i), _mm256_sub_epi8(in1, in2));
			}
			for (size_t i = size16; i < bytes; ++i)
				dst[i] = src[i] - src[i - 1];
		}
		else {
			size_t bytes4 = bytes / 4;
			const char* s[4] = { src, src + bytes4, src + bytes4 * 2, src + bytes4 * 3 };
			char* d[4] = { dst, dst + bytes4, dst + bytes4 * 2, dst + bytes4 * 3 };

			*d[0]++ = *s[0]++;
			*d[1]++ = *s[1]++;
			*d[2]++ = *s[2]++;
			*d[3]++ = *s[3]++;

			--bytes4;
			size_t size32 = bytes4 & ~(31ull);
			for (size_t i = 0; i < size32; i += 32) {
				__m256i in1 = _mm256_loadu_si256((const __m256i*)(s[0] + i));
				__m256i in2 = _mm256_loadu_si256((const __m256i*)(s[0] + i - 1));
				_mm256_storeu_si256((__m256i*)(d[0] + i), _mm256_sub_epi8(in1, in2));

				in1 = _mm256_loadu_si256((const __m256i*)(s[1] + i));
				in2 = _mm256_loadu_si256((const __m256i*)(s[1] + i - 1));
				_mm256_storeu_si256((__m256i*)(d[1] + i), _mm256_sub_epi8(in1, in2));

				in1 = _mm256_loadu_si256((const __m256i*)(s[2] + i));
				in2 = _mm256_loadu_si256((const __m256i*)(s[2] + i - 1));
				_mm256_storeu_si256((__m256i*)(d[2] + i), _mm256_sub_epi8(in1, in2));

				in1 = _mm256_loadu_si256((const __m256i*)(s[3] + i));
				in2 = _mm256_loadu_si256((const __m256i*)(s[3] + i - 1));
				_mm256_storeu_si256((__m256i*)(d[3] + i), _mm256_sub_epi8(in1, in2));
			}
			for (size_t i = size32; i < bytes4; ++i) {
				d[0][i] = s[0][i] - s[0][i - 1];
				d[1][i] = s[1][i] - s[1][i - 1];
				d[2][i] = s[2][i] - s[2][i - 1];
				d[3][i] = s[3][i] - s[3][i - 1];
			}

			size_t start = (bytes4 + 1) * 4;
			for (; start != bytes; ++start)
				dst[start] = src[start] - src[start - 1];
		}
	}
#endif

	void delta(const void* _src, void* _dst, size_t bytes)
	{

#ifdef __AVX2__
		if (cpu_features().HAS_AVX2)
			return delta_avx2(_src, _dst, bytes);
#endif

#ifdef __SSE2__
		if (cpu_features().HAS_SSE2)
			return delta_sse2(_src, _dst, bytes);
#endif

		delta_generic(_src, _dst, bytes);
	}

	//
	// Inverse byte delta
	//

	static inline void delta_inv_generic(const void* _src, void* _dst, size_t bytes)
	{
		const char* src = (const char*)_src;
		char* dst = (char*)_dst;

		if (bytes == 0)
			return;

		if (bytes <= 2048) {
			*dst++ = *src++;
			--bytes;
			for (size_t i = 0; i < bytes; ++i) {
				dst[i] = dst[i - 1] + src[i];
			}
		}
		else {
			size_t bytes4 = bytes / 4;
			const char* s[4] = { src, src + bytes4, src + bytes4 * 2, src + bytes4 * 3 };
			char* d[4] = { dst, dst + bytes4, dst + bytes4 * 2, dst + bytes4 * 3 };

			*d[0]++ = *s[0]++;
			*d[1]++ = *s[1]++;
			*d[2]++ = *s[2]++;
			*d[3]++ = *s[3]++;

			--bytes4;
			for (size_t i = 0; i < bytes4; ++i) {
				d[0][i] = d[0][i - 1] + s[0][i];
				d[1][i] = d[1][i - 1] + s[1][i];
				d[2][i] = d[2][i - 1] + s[2][i];
				d[3][i] = d[3][i - 1] + s[3][i];
			}

			size_t start = (bytes4 + 1) * 4;
			for (; start != bytes; ++start)
				dst[start] = dst[start - 1] + src[start];
		}
	}

#ifdef __SSE2__

	static STENOS_ALWAYS_INLINE __m128i prefix_sum_16(__m128i x) noexcept
	{
		// Prefix sum on 16 bytes
		x = _mm_add_epi8(x, _mm_slli_si128(x, 1));
		x = _mm_add_epi8(x, _mm_slli_si128(x, 2));
		x = _mm_add_epi8(x, _mm_slli_si128(x, 4));
		x = _mm_add_epi8(x, _mm_slli_si128(x, 8));
		return x;
	}
	static inline void delta_inv_sse2(const void* _src, void* _dst, size_t bytes)
	{
		const char* src = (const char*)_src;
		char* dst = (char*)_dst;

		if (bytes == 0)
			return;

		__m128i mask = _mm_setr_epi8((char)0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		if (bytes <= 2048) {

			auto end = src + bytes;
			__m128i first = _mm_setzero_si128();
			while (src + 15 < end) {
				__m128i row = _mm_loadu_si128((const __m128i*)(src));
				_mm_storeu_si128((__m128i*)(dst), prefix_sum_16(_mm_add_epi8(_mm_and_si128(mask, first), row)));
				first = _mm_set1_epi8(dst[15]);
				src += 16;
				dst += 16;
			}
			while (src < end) {
				*dst = dst[-1] + *src;
				++src;
				++dst;
			}
		}
		else {
			size_t bytes4 = bytes / 4;
			const char* s[4] = { src, src + bytes4, src + bytes4 * 2, src + bytes4 * 3 };
			char* d[4] = { dst, dst + bytes4, dst + bytes4 * 2, dst + bytes4 * 3 };
			__m128i first[4] = { _mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128(), _mm_setzero_si128() };
			const char* en = src + bytes4;

			while (s[0] + 15 < en) {
				__m128i row = _mm_loadu_si128((const __m128i*)(s[0]));
				_mm_storeu_si128((__m128i*)(d[0]), prefix_sum_16(_mm_add_epi8(_mm_and_si128(mask, first[0]), row)));
				first[0] = _mm_set1_epi8(d[0][15]);
				s[0] += 16;
				d[0] += 16;

				row = _mm_loadu_si128((const __m128i*)(s[1]));
				_mm_storeu_si128((__m128i*)(d[1]), prefix_sum_16(_mm_add_epi8(_mm_and_si128(mask, first[1]), row)));
				first[1] = _mm_set1_epi8(d[1][15]);
				s[1] += 16;
				d[1] += 16;

				row = _mm_loadu_si128((const __m128i*)(s[2]));
				_mm_storeu_si128((__m128i*)(d[2]), prefix_sum_16(_mm_add_epi8(_mm_and_si128(mask, first[2]), row)));
				first[2] = _mm_set1_epi8(d[2][15]);
				s[2] += 16;
				d[2] += 16;

				row = _mm_loadu_si128((const __m128i*)(s[3]));
				_mm_storeu_si128((__m128i*)(d[3]), prefix_sum_16(_mm_add_epi8(_mm_and_si128(mask, first[3]), row)));
				first[3] = _mm_set1_epi8(d[3][15]);
				s[3] += 16;
				d[3] += 16;
			}
			while (s[0] < en) {
				*d[0] = d[0][-1] + *s[0];
				++s[0];
				++d[0];

				*d[1] = d[1][-1] + *s[1];
				++s[1];
				++d[1];

				*d[2] = d[2][-1] + *s[2];
				++s[2];
				++d[2];

				*d[3] = d[3][-1] + *s[3];
				++s[3];
				++d[3];
			}

			size_t start = bytes4 * 4;
			for (; start != bytes; ++start)
				dst[start] = dst[start - 1] + src[start];
		}
	}
#endif

#ifdef __AVX2__

	static STENOS_ALWAYS_INLINE __m256i prefix2(__m256i x)
	{
		// Prefix sum on 2 x 16 bytes.
		// The prefix sum on 32 bytes is computed by adding the 15th byte to the 16 right bytes.
		x = _mm256_add_epi8(x, _mm256_slli_si256(x, 1));
		x = _mm256_add_epi8(x, _mm256_slli_si256(x, 2));
		x = _mm256_add_epi8(x, _mm256_slli_si256(x, 4));
		x = _mm256_add_epi8(x, _mm256_slli_si256(x, 8));
		return x;
	}

	static STENOS_ALWAYS_INLINE __m256i prefix_sum_32(__m256i x, __m256i shuffle_mask) noexcept
	{
		x = prefix2(x);
		// Add the 15th byte to the 16 right bytes.
		auto last_value = _mm256_permute4x64_epi64(x, _MM_SHUFFLE(1, 0, 0, 0));
		last_value = _mm256_shuffle_epi8(last_value, shuffle_mask);
		x = _mm256_add_epi8(x, last_value);
		return x;
	}
	static inline void delta_inv_avx2(const void* _src, void* _dst, size_t bytes)
	{
		const char* src = (const char*)_src;
		char* dst = (char*)_dst;

		if (bytes == 0)
			return;

		const __m256i mask = _mm256_setr_epi8((char)0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		const __m256i shuffle = _mm256_set_m128i(_mm_set1_epi8(15), _mm_set1_epi8((char)0x80));

		if (bytes <= 2048) {

			auto end = src + bytes;
			__m256i first = _mm256_setzero_si256();
			while (src + 31 < end) {
				__m256i row = _mm256_loadu_si256((const __m256i*)(src));
				_mm256_storeu_si256((__m256i*)(dst), prefix_sum_32(_mm256_add_epi8(_mm256_and_si256(mask, first), row), shuffle));
				first = _mm256_set1_epi8(dst[31]);
				src += 32;
				dst += 32;
			}
			while (src < end) {
				*dst = dst[-1] + *src;
				++src;
				++dst;
			}
		}
		else {
			size_t bytes4 = bytes / 4;
			const char* s[4] = { src, src + bytes4, src + bytes4 * 2, src + bytes4 * 3 };
			char* d[4] = { dst, dst + bytes4, dst + bytes4 * 2, dst + bytes4 * 3 };
			__m256i first[4] = { _mm256_setzero_si256(), _mm256_setzero_si256(), _mm256_setzero_si256(), _mm256_setzero_si256() };
			const char* en = src + bytes4;

			while (s[0] + 31 < en) {
				__m256i row = _mm256_loadu_si256((const __m256i*)(s[0]));
				_mm256_storeu_si256((__m256i*)(d[0]), prefix_sum_32(_mm256_add_epi8(_mm256_and_si256(mask, first[0]), row), shuffle));
				first[0] = _mm256_set1_epi8(d[0][31]);
				s[0] += 32;
				d[0] += 32;

				row = _mm256_loadu_si256((const __m256i*)(s[1]));
				_mm256_storeu_si256((__m256i*)(d[1]), prefix_sum_32(_mm256_add_epi8(_mm256_and_si256(mask, first[1]), row), shuffle));
				first[1] = _mm256_set1_epi8(d[1][31]);
				s[1] += 32;
				d[1] += 32;

				row = _mm256_loadu_si256((const __m256i*)(s[2]));
				_mm256_storeu_si256((__m256i*)(d[2]), prefix_sum_32(_mm256_add_epi8(_mm256_and_si256(mask, first[2]), row), shuffle));
				first[2] = _mm256_set1_epi8(d[2][31]);
				s[2] += 32;
				d[2] += 32;

				row = _mm256_loadu_si256((const __m256i*)(s[3]));
				_mm256_storeu_si256((__m256i*)(d[3]), prefix_sum_32(_mm256_add_epi8(_mm256_and_si256(mask, first[3]), row), shuffle));
				first[3] = _mm256_set1_epi8(d[3][31]);
				s[3] += 32;
				d[3] += 32;
			}
			while (s[0] < en) {
				*d[0] = d[0][-1] + *s[0];
				++s[0];
				++d[0];

				*d[1] = d[1][-1] + *s[1];
				++s[1];
				++d[1];

				*d[2] = d[2][-1] + *s[2];
				++s[2];
				++d[2];

				*d[3] = d[3][-1] + *s[3];
				++s[3];
				++d[3];
			}

			size_t start = bytes4 * 4;
			for (; start != bytes; ++start)
				dst[start] = dst[start - 1] + src[start];
		}
	}

#endif

	void delta_inv(const void* src, void* dst, size_t bytes)
	{

#ifdef __AVX2__
		if (cpu_features().HAS_AVX2)
			return delta_inv_avx2(src, dst, bytes);
#endif

#ifdef __SSE2__
		if (cpu_features().HAS_SSE2)
			return delta_inv_sse2(src, dst, bytes);
#endif

		delta_inv_generic(src, dst, bytes);
	}
}