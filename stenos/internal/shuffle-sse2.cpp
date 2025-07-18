/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Copyright (c) 2021  Blosc Development Team <blosc@blosc.org>
  https://blosc.org
  License: BSD 3-Clause (see LICENSE.txt)

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <utility>

#include "shuffle-sse2.h"
#include "shuffle-generic.h"
#include "simd.h"

/* Make sure SSE2 is available for the compilation target and compiler. */
#if defined(__SSE2__)

/* The next is useful for debugging purposes */
#if 0
#include <stdio.h>
#include <string.h>

static void printxmm(__m128i xmm0)
{
  uint8_t buf[16];

  ((__m128i *)buf)[0] = xmm0;
  printf("%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n",
          buf[0], buf[1], buf[2], buf[3],
          buf[4], buf[5], buf[6], buf[7],
          buf[8], buf[9], buf[10], buf[11],
          buf[12], buf[13], buf[14], buf[15]);
}
#endif

/* Routine optimized for shuffling a buffer for a type size of 2 bytes. */
static void shuffle2_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 2;
	int32_t j;
	int k;
	uint8_t* dest_for_jth_element;
	__m128i xmm0[2], xmm1[2];

	for (j = 0; j < vectorizable_elements; j += sizeof(__m128i)) {
		/* Fetch 16 elements (32 bytes) then transpose bytes, words and double words. */
		for (k = 0; k < 2; k++) {
			xmm0[k] = _mm_loadu_si128((__m128i*)(src + (j * bytesoftype) + (k * sizeof(__m128i))));
			xmm0[k] = _mm_shufflelo_epi16(xmm0[k], 0xd8);
			xmm0[k] = _mm_shufflehi_epi16(xmm0[k], 0xd8);
			xmm0[k] = _mm_shuffle_epi32(xmm0[k], 0xd8);
			xmm1[k] = _mm_shuffle_epi32(xmm0[k], 0x4e);
			xmm0[k] = _mm_unpacklo_epi8(xmm0[k], xmm1[k]);
			xmm0[k] = _mm_shuffle_epi32(xmm0[k], 0xd8);
			xmm1[k] = _mm_shuffle_epi32(xmm0[k], 0x4e);
			xmm0[k] = _mm_unpacklo_epi16(xmm0[k], xmm1[k]);
			xmm0[k] = _mm_shuffle_epi32(xmm0[k], 0xd8);
		}
		/* Transpose quad words */
		for (k = 0; k < 1; k++) {
			xmm1[k * 2] = _mm_unpacklo_epi64(xmm0[k], xmm0[k + 1]);
			xmm1[k * 2 + 1] = _mm_unpackhi_epi64(xmm0[k], xmm0[k + 1]);
		}
		/* Store the result vectors */
		dest_for_jth_element = dest + j;
		for (k = 0; k < 2; k++) {
			_mm_storeu_si128((__m128i*)(dest_for_jth_element + (k * total_elements)), xmm1[k]);
		}
	}
}

/* Routine optimized for shuffling a buffer for a type size of 4 bytes. */
static void shuffle4_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 4;
	int32_t i;
	int j;
	uint8_t* dest_for_ith_element;
	__m128i xmm0[4], xmm1[4];

	for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
		/* Fetch 16 elements (64 bytes) then transpose bytes and words. */
		for (j = 0; j < 4; j++) {
			xmm0[j] = _mm_loadu_si128((__m128i*)(src + (i * bytesoftype) + (j * sizeof(__m128i))));
			xmm1[j] = _mm_shuffle_epi32(xmm0[j], 0xd8);
			xmm0[j] = _mm_shuffle_epi32(xmm0[j], 0x8d);
			xmm0[j] = _mm_unpacklo_epi8(xmm1[j], xmm0[j]);
			xmm1[j] = _mm_shuffle_epi32(xmm0[j], 0x04e);
			xmm0[j] = _mm_unpacklo_epi16(xmm0[j], xmm1[j]);
		}
		/* Transpose double words */
		for (j = 0; j < 2; j++) {
			xmm1[j * 2] = _mm_unpacklo_epi32(xmm0[j * 2], xmm0[j * 2 + 1]);
			xmm1[j * 2 + 1] = _mm_unpackhi_epi32(xmm0[j * 2], xmm0[j * 2 + 1]);
		}
		/* Transpose quad words */
		for (j = 0; j < 2; j++) {
			xmm0[j * 2] = _mm_unpacklo_epi64(xmm1[j], xmm1[j + 2]);
			xmm0[j * 2 + 1] = _mm_unpackhi_epi64(xmm1[j], xmm1[j + 2]);
		}
		/* Store the result vectors */
		dest_for_ith_element = dest + i;
		for (j = 0; j < 4; j++) {
			_mm_storeu_si128((__m128i*)(dest_for_ith_element + (j * total_elements)), xmm0[j]);
		}
	}
}

/* Routine optimized for shuffling a buffer for a type size of 8 bytes. */
static void shuffle8_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 8;
	int32_t j;
	int k, l;
	uint8_t* dest_for_jth_element;
	__m128i xmm0[8], xmm1[8];

	for (j = 0; j < vectorizable_elements; j += sizeof(__m128i)) {
		/* Fetch 16 elements (128 bytes) then transpose bytes. */
		for (k = 0; k < 8; k++) {
			xmm0[k] = _mm_loadu_si128((__m128i*)(src + (j * bytesoftype) + (k * sizeof(__m128i))));
			xmm1[k] = _mm_shuffle_epi32(xmm0[k], 0x4e);
			xmm1[k] = _mm_unpacklo_epi8(xmm0[k], xmm1[k]);
		}
		/* Transpose words */
		for (k = 0, l = 0; k < 4; k++, l += 2) {
			xmm0[k * 2] = _mm_unpacklo_epi16(xmm1[l], xmm1[l + 1]);
			xmm0[k * 2 + 1] = _mm_unpackhi_epi16(xmm1[l], xmm1[l + 1]);
		}
		/* Transpose double words */
		for (k = 0, l = 0; k < 4; k++, l++) {
			if (k == 2)
				l += 2;
			xmm1[k * 2] = _mm_unpacklo_epi32(xmm0[l], xmm0[l + 2]);
			xmm1[k * 2 + 1] = _mm_unpackhi_epi32(xmm0[l], xmm0[l + 2]);
		}
		/* Transpose quad words */
		for (k = 0; k < 4; k++) {
			xmm0[k * 2] = _mm_unpacklo_epi64(xmm1[k], xmm1[k + 4]);
			xmm0[k * 2 + 1] = _mm_unpackhi_epi64(xmm1[k], xmm1[k + 4]);
		}
		/* Store the result vectors */
		dest_for_jth_element = dest + j;
		for (k = 0; k < 8; k++) {
			_mm_storeu_si128((__m128i*)(dest_for_jth_element + (k * total_elements)), xmm0[k]);
		}
	}
}

/* Routine optimized for shuffling a buffer for a type size of 16 bytes. */
static void shuffle16_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 16;
	int32_t j;
	int k, l;
	uint8_t* dest_for_jth_element;
	__m128i xmm0[16], xmm1[16];

	for (j = 0; j < vectorizable_elements; j += sizeof(__m128i)) {
		/* Fetch 16 elements (256 bytes). */
		for (k = 0; k < 16; k++) {
			xmm0[k] = _mm_loadu_si128((__m128i*)(src + (j * bytesoftype) + (k * sizeof(__m128i))));
		}
		/* Transpose bytes */
		for (k = 0, l = 0; k < 8; k++, l += 2) {
			xmm1[k * 2] = _mm_unpacklo_epi8(xmm0[l], xmm0[l + 1]);
			xmm1[k * 2 + 1] = _mm_unpackhi_epi8(xmm0[l], xmm0[l + 1]);
		}
		/* Transpose words */
		for (k = 0, l = -2; k < 8; k++, l++) {
			if ((k % 2) == 0)
				l += 2;
			xmm0[k * 2] = _mm_unpacklo_epi16(xmm1[l], xmm1[l + 2]);
			xmm0[k * 2 + 1] = _mm_unpackhi_epi16(xmm1[l], xmm1[l + 2]);
		}
		/* Transpose double words */
		for (k = 0, l = -4; k < 8; k++, l++) {
			if ((k % 4) == 0)
				l += 4;
			xmm1[k * 2] = _mm_unpacklo_epi32(xmm0[l], xmm0[l + 4]);
			xmm1[k * 2 + 1] = _mm_unpackhi_epi32(xmm0[l], xmm0[l + 4]);
		}
		/* Transpose quad words */
		for (k = 0; k < 8; k++) {
			xmm0[k * 2] = _mm_unpacklo_epi64(xmm1[k], xmm1[k + 8]);
			xmm0[k * 2 + 1] = _mm_unpackhi_epi64(xmm1[k], xmm1[k + 8]);
		}
		/* Store the result vectors */
		dest_for_jth_element = dest + j;
		for (k = 0; k < 16; k++) {
			_mm_storeu_si128((__m128i*)(dest_for_jth_element + (k * total_elements)), xmm0[k]);
		}
	}
}

/* Routine optimized for shuffling a buffer for a type size larger than 16 bytes. */
static void shuffle16_tiled_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements, const int32_t bytesoftype)
{
	int32_t j;
	const int32_t vecs_per_el_rem = bytesoftype % (int32_t)sizeof(__m128i);
	int k, l;
	uint8_t* dest_for_jth_element;
	__m128i xmm0[16], xmm1[16];

	for (j = 0; j < vectorizable_elements; j += sizeof(__m128i)) {
		/* Advance the offset into the type by the vector size (in bytes), unless this is
		the initial iteration and the type size is not a multiple of the vector size.
		In that case, only advance by the number of bytes necessary so that the number
		of remaining bytes in the type will be a multiple of the vector size. */
		int32_t offset_into_type;
		for (offset_into_type = 0; offset_into_type < bytesoftype; offset_into_type += (offset_into_type == 0 && vecs_per_el_rem > 0 ? vecs_per_el_rem : (int32_t)sizeof(__m128i))) {

			/* Fetch elements in groups of 256 bytes */
			const uint8_t* const src_with_offset = src + offset_into_type;
			for (k = 0; k < 16; k++) {
				xmm0[k] = _mm_loadu_si128((__m128i*)(src_with_offset + (j + k) * bytesoftype));
			}
			/* Transpose bytes */
			for (k = 0, l = 0; k < 8; k++, l += 2) {
				xmm1[k * 2] = _mm_unpacklo_epi8(xmm0[l], xmm0[l + 1]);
				xmm1[k * 2 + 1] = _mm_unpackhi_epi8(xmm0[l], xmm0[l + 1]);
			}
			/* Transpose words */
			for (k = 0, l = -2; k < 8; k++, l++) {
				if ((k % 2) == 0)
					l += 2;
				xmm0[k * 2] = _mm_unpacklo_epi16(xmm1[l], xmm1[l + 2]);
				xmm0[k * 2 + 1] = _mm_unpackhi_epi16(xmm1[l], xmm1[l + 2]);
			}
			/* Transpose double words */
			for (k = 0, l = -4; k < 8; k++, l++) {
				if ((k % 4) == 0)
					l += 4;
				xmm1[k * 2] = _mm_unpacklo_epi32(xmm0[l], xmm0[l + 4]);
				xmm1[k * 2 + 1] = _mm_unpackhi_epi32(xmm0[l], xmm0[l + 4]);
			}
			/* Transpose quad words */
			for (k = 0; k < 8; k++) {
				xmm0[k * 2] = _mm_unpacklo_epi64(xmm1[k], xmm1[k + 8]);
				xmm0[k * 2 + 1] = _mm_unpackhi_epi64(xmm1[k], xmm1[k + 8]);
			}
			/* Store the result vectors */
			dest_for_jth_element = dest + j;
			for (k = 0; k < 16; k++) {
				_mm_storeu_si128((__m128i*)(dest_for_jth_element + (total_elements * (offset_into_type + k))), xmm0[k]);
			}
		}
	}
}

/* Routine optimized for unshuffling a buffer for a type size of 2 bytes. */
static void unshuffle2_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 2;
	int32_t i;
	int j;
	__m128i xmm0[2], xmm1[2];

	for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
		/* Load 16 elements (32 bytes) into 2 XMM registers. */
		const uint8_t* const src_for_ith_element = src + i;
		for (j = 0; j < 2; j++) {
			xmm0[j] = _mm_loadu_si128((__m128i*)(src_for_ith_element + (j * total_elements)));
		}
		/* Shuffle bytes */
		/* Compute the low 32 bytes */
		xmm1[0] = _mm_unpacklo_epi8(xmm0[0], xmm0[1]);
		/* Compute the hi 32 bytes */
		xmm1[1] = _mm_unpackhi_epi8(xmm0[0], xmm0[1]);
		/* Store the result vectors in proper order */
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (0 * sizeof(__m128i))), xmm1[0]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (1 * sizeof(__m128i))), xmm1[1]);
	}
}

/* Routine optimized for unshuffling a buffer for a type size of 4 bytes. */
static void unshuffle4_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 4;
	int32_t i;
	int j;
	__m128i xmm0[4], xmm1[4];

	for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
		/* Load 16 elements (64 bytes) into 4 XMM registers. */
		const uint8_t* const src_for_ith_element = src + i;
		for (j = 0; j < 4; j++) {
			xmm0[j] = _mm_loadu_si128((__m128i*)(src_for_ith_element + (j * total_elements)));
		}
		/* Shuffle bytes */
		for (j = 0; j < 2; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi8(xmm0[j * 2], xmm0[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[2 + j] = _mm_unpackhi_epi8(xmm0[j * 2], xmm0[j * 2 + 1]);
		}
		/* Shuffle 2-byte words */
		for (j = 0; j < 2; j++) {
			/* Compute the low 32 bytes */
			xmm0[j] = _mm_unpacklo_epi16(xmm1[j * 2], xmm1[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm0[2 + j] = _mm_unpackhi_epi16(xmm1[j * 2], xmm1[j * 2 + 1]);
		}
		/* Store the result vectors in proper order */
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (0 * sizeof(__m128i))), xmm0[0]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (1 * sizeof(__m128i))), xmm0[2]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (2 * sizeof(__m128i))), xmm0[1]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (3 * sizeof(__m128i))), xmm0[3]);
	}
}

/* Routine optimized for unshuffling a buffer for a type size of 8 bytes. */
static void unshuffle8_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 8;
	int32_t i;
	int j;
	__m128i xmm0[8], xmm1[8];

	for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
		/* Load 16 elements (128 bytes) into 8 XMM registers. */
		const uint8_t* const src_for_ith_element = src + i;
		for (j = 0; j < 8; j++) {
			xmm0[j] = _mm_loadu_si128((__m128i*)(src_for_ith_element + (j * total_elements)));
		}
		/* Shuffle bytes */
		for (j = 0; j < 4; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi8(xmm0[j * 2], xmm0[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[4 + j] = _mm_unpackhi_epi8(xmm0[j * 2], xmm0[j * 2 + 1]);
		}
		/* Shuffle 2-byte words */
		for (j = 0; j < 4; j++) {
			/* Compute the low 32 bytes */
			xmm0[j] = _mm_unpacklo_epi16(xmm1[j * 2], xmm1[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm0[4 + j] = _mm_unpackhi_epi16(xmm1[j * 2], xmm1[j * 2 + 1]);
		}
		/* Shuffle 4-byte dwords */
		for (j = 0; j < 4; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi32(xmm0[j * 2], xmm0[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[4 + j] = _mm_unpackhi_epi32(xmm0[j * 2], xmm0[j * 2 + 1]);
		}
		/* Store the result vectors in proper order */
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (0 * sizeof(__m128i))), xmm1[0]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (1 * sizeof(__m128i))), xmm1[4]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (2 * sizeof(__m128i))), xmm1[2]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (3 * sizeof(__m128i))), xmm1[6]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (4 * sizeof(__m128i))), xmm1[1]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (5 * sizeof(__m128i))), xmm1[5]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (6 * sizeof(__m128i))), xmm1[3]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (7 * sizeof(__m128i))), xmm1[7]);
	}
}

template<class... Args>
STENOS_ALWAYS_INLINE __m128i setr_epi8(Args... values) noexcept
{
	return _mm_setr_epi8(std::forward<char>(values)...);
}

/* Routine optimized for unshuffling a buffer for a type size of 12 bytes. */
/* Based on the 16-byte implementation */
static void unshuffle12_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 12;
	int32_t i;
	int j;
	__m128i xmm1[16], xmm2[16];

	__m128i mask = setr_epi8(0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);

	for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
		/* Load 16 elements (256 bytes) into 16 XMM registers. */
		const uint8_t* const src_for_ith_element = src + i;
		for (j = 0; j < bytesoftype; j++) {
			xmm1[j] = _mm_loadu_si128((__m128i*)(src_for_ith_element + (j * total_elements)));
		}
		/* Shuffle bytes */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm2[j] = _mm_unpacklo_epi8(xmm1[j * 2], xmm1[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm2[8 + j] = _mm_unpackhi_epi8(xmm1[j * 2], xmm1[j * 2 + 1]);
		}
		/* Shuffle 2-byte words */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi16(xmm2[j * 2], xmm2[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[8 + j] = _mm_unpackhi_epi16(xmm2[j * 2], xmm2[j * 2 + 1]);
		}
		/* Shuffle 4-byte dwords */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm2[j] = _mm_unpacklo_epi32(xmm1[j * 2], xmm1[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm2[8 + j] = _mm_unpackhi_epi32(xmm1[j * 2], xmm1[j * 2 + 1]);
		}
		/* Shuffle 8-byte qwords */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi64(xmm2[j * 2], xmm2[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[8 + j] = _mm_unpackhi_epi64(xmm2[j * 2], xmm2[j * 2 + 1]);
		}

		/* Store the result vectors in proper order */
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (0 * 12)), xmm1[0]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (1 * 12)), xmm1[8]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (2 * 12)), xmm1[4]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (3 * 12)), xmm1[12]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (4 * 12)), xmm1[2]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (5 * 12)), xmm1[10]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (6 * 12)), xmm1[6]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (7 * 12)), xmm1[14]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (8 * 12)), xmm1[1]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (9 * 12)), xmm1[9]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (10 * 12)), xmm1[5]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (11 * 12)), xmm1[13]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (12 * 12)), xmm1[3]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (13 * 12)), xmm1[11]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (14 * 12)), xmm1[7]);
		_mm_maskmoveu_si128(xmm1[15], mask, (char*)(dest + (i * bytesoftype) + (15 * 12)));
	}
}

/* Routine optimized for unshuffling a buffer for a type size of 16 bytes. */
static void unshuffle16_sse2(uint8_t* const dest, const uint8_t* const src, const int32_t vectorizable_elements, const int32_t total_elements)
{
	static const int32_t bytesoftype = 16;
	int32_t i;
	int j;
	__m128i xmm1[16], xmm2[16];

	for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
		/* Load 16 elements (256 bytes) into 16 XMM registers. */
		const uint8_t* const src_for_ith_element = src + i;
		for (j = 0; j < 16; j++) {
			xmm1[j] = _mm_loadu_si128((__m128i*)(src_for_ith_element + (j * total_elements)));
		}
		/* Shuffle bytes */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm2[j] = _mm_unpacklo_epi8(xmm1[j * 2], xmm1[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm2[8 + j] = _mm_unpackhi_epi8(xmm1[j * 2], xmm1[j * 2 + 1]);
		}
		/* Shuffle 2-byte words */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi16(xmm2[j * 2], xmm2[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[8 + j] = _mm_unpackhi_epi16(xmm2[j * 2], xmm2[j * 2 + 1]);
		}
		/* Shuffle 4-byte dwords */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm2[j] = _mm_unpacklo_epi32(xmm1[j * 2], xmm1[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm2[8 + j] = _mm_unpackhi_epi32(xmm1[j * 2], xmm1[j * 2 + 1]);
		}
		/* Shuffle 8-byte qwords */
		for (j = 0; j < 8; j++) {
			/* Compute the low 32 bytes */
			xmm1[j] = _mm_unpacklo_epi64(xmm2[j * 2], xmm2[j * 2 + 1]);
			/* Compute the hi 32 bytes */
			xmm1[8 + j] = _mm_unpackhi_epi64(xmm2[j * 2], xmm2[j * 2 + 1]);
		}

		/* Store the result vectors in proper order */
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (0 * sizeof(__m128i))), xmm1[0]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (1 * sizeof(__m128i))), xmm1[8]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (2 * sizeof(__m128i))), xmm1[4]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (3 * sizeof(__m128i))), xmm1[12]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (4 * sizeof(__m128i))), xmm1[2]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (5 * sizeof(__m128i))), xmm1[10]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (6 * sizeof(__m128i))), xmm1[6]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (7 * sizeof(__m128i))), xmm1[14]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (8 * sizeof(__m128i))), xmm1[1]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (9 * sizeof(__m128i))), xmm1[9]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (10 * sizeof(__m128i))), xmm1[5]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (11 * sizeof(__m128i))), xmm1[13]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (12 * sizeof(__m128i))), xmm1[3]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (13 * sizeof(__m128i))), xmm1[11]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (14 * sizeof(__m128i))), xmm1[7]);
		_mm_storeu_si128((__m128i*)(dest + (i * bytesoftype) + (15 * sizeof(__m128i))), xmm1[15]);
	}
}

/* Routine optimized for unshuffling a buffer for a type size larger than 16 bytes. */
static void unshuffle16_tiled_sse2(uint8_t* const dest, const uint8_t* const orig, const int32_t vectorizable_elements, const int32_t total_elements, const int32_t bytesoftype)
{
	int32_t i;
	const int32_t vecs_per_el_rem = bytesoftype % (int32_t)sizeof(__m128i);

	int j;
	uint8_t* dest_with_offset;
	__m128i xmm1[16], xmm2[16];

	/* The unshuffle loops are inverted (compared to shuffle_tiled16_sse2)
	   to optimize cache utilization. */
	int32_t offset_into_type;
	for (offset_into_type = 0; offset_into_type < bytesoftype; offset_into_type += (offset_into_type == 0 && vecs_per_el_rem > 0 ? vecs_per_el_rem : (int32_t)sizeof(__m128i))) {
		for (i = 0; i < vectorizable_elements; i += sizeof(__m128i)) {
			/* Load the first 128 bytes in 16 XMM registers */
			const uint8_t* const src_for_ith_element = orig + i;
			for (j = 0; j < 16; j++) {
				xmm1[j] = _mm_loadu_si128((__m128i*)(src_for_ith_element + (total_elements * (offset_into_type + j))));
			}
			/* Shuffle bytes */
			for (j = 0; j < 8; j++) {
				/* Compute the low 32 bytes */
				xmm2[j] = _mm_unpacklo_epi8(xmm1[j * 2], xmm1[j * 2 + 1]);
				/* Compute the hi 32 bytes */
				xmm2[8 + j] = _mm_unpackhi_epi8(xmm1[j * 2], xmm1[j * 2 + 1]);
			}
			/* Shuffle 2-byte words */
			for (j = 0; j < 8; j++) {
				/* Compute the low 32 bytes */
				xmm1[j] = _mm_unpacklo_epi16(xmm2[j * 2], xmm2[j * 2 + 1]);
				/* Compute the hi 32 bytes */
				xmm1[8 + j] = _mm_unpackhi_epi16(xmm2[j * 2], xmm2[j * 2 + 1]);
			}
			/* Shuffle 4-byte dwords */
			for (j = 0; j < 8; j++) {
				/* Compute the low 32 bytes */
				xmm2[j] = _mm_unpacklo_epi32(xmm1[j * 2], xmm1[j * 2 + 1]);
				/* Compute the hi 32 bytes */
				xmm2[8 + j] = _mm_unpackhi_epi32(xmm1[j * 2], xmm1[j * 2 + 1]);
			}
			/* Shuffle 8-byte qwords */
			for (j = 0; j < 8; j++) {
				/* Compute the low 32 bytes */
				xmm1[j] = _mm_unpacklo_epi64(xmm2[j * 2], xmm2[j * 2 + 1]);
				/* Compute the hi 32 bytes */
				xmm1[8 + j] = _mm_unpackhi_epi64(xmm2[j * 2], xmm2[j * 2 + 1]);
			}

			/* Store the result vectors in proper order */
			dest_with_offset = dest + offset_into_type;
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 0) * bytesoftype), xmm1[0]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 1) * bytesoftype), xmm1[8]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 2) * bytesoftype), xmm1[4]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 3) * bytesoftype), xmm1[12]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 4) * bytesoftype), xmm1[2]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 5) * bytesoftype), xmm1[10]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 6) * bytesoftype), xmm1[6]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 7) * bytesoftype), xmm1[14]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 8) * bytesoftype), xmm1[1]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 9) * bytesoftype), xmm1[9]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 10) * bytesoftype), xmm1[5]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 11) * bytesoftype), xmm1[13]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 12) * bytesoftype), xmm1[3]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 13) * bytesoftype), xmm1[11]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 14) * bytesoftype), xmm1[7]);
			_mm_storeu_si128((__m128i*)(dest_with_offset + (i + 15) * bytesoftype), xmm1[15]);
		}
	}
}

/* Shuffle a block.  This can never fail. */
void shuffle_sse2(int32_t bytesoftype, int32_t blocksize, const uint8_t* _src, uint8_t* _dest)
{
	const int32_t vectorized_chunk_size = bytesoftype * (int32_t)sizeof(__m128i);
	/* If the blocksize is not a multiple of both the typesize and
	   the vector size, round the blocksize down to the next value
	   which is a multiple of both. The vectorized shuffle can be
	   used for that portion of the data, and the naive implementation
	   can be used for the remaining portion. */
	const int32_t vectorizable_bytes = blocksize - (blocksize % vectorized_chunk_size);
	const int32_t vectorizable_elements = vectorizable_bytes / bytesoftype;
	const int32_t total_elements = blocksize / bytesoftype;

	/* If the block size is too small to be vectorized,
	   use the generic implementation. */
	if (blocksize < vectorized_chunk_size) {
		shuffle_generic(bytesoftype, blocksize, _src, _dest);
		return;
	}

	/* Optimized shuffle implementations */
	switch (bytesoftype) {
		case 2:
			shuffle2_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 4:
			shuffle4_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 8:
			shuffle8_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 16:
			shuffle16_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		default:
			if (bytesoftype > (int32_t)sizeof(__m128i)) {
				shuffle16_tiled_sse2(_dest, _src, vectorizable_elements, total_elements, bytesoftype);
			}
			else {
				/* Non-optimized shuffle */
				shuffle_generic(bytesoftype, blocksize, _src, _dest);
				/* The non-optimized function covers the whole buffer,
				   so we're done processing here. */
				return;
			}
	}

	/* If the buffer had any bytes at the end which couldn't be handled
	   by the vectorized implementations, use the non-optimized version
	   to finish them up. */
	if (vectorizable_bytes < blocksize) {
		shuffle_generic_inline(bytesoftype, vectorizable_bytes, blocksize, _src, _dest);
	}
}

/* Unshuffle a block.  This can never fail. */
void unshuffle_sse2(int32_t bytesoftype, int32_t blocksize, const uint8_t* _src, uint8_t* _dest)
{
	const int32_t vectorized_chunk_size = bytesoftype * (int32_t)sizeof(__m128i);
	/* If the blocksize is not a multiple of both the typesize and
	   the vector size, round the blocksize down to the next value
	   which is a multiple of both. The vectorized unshuffle can be
	   used for that portion of the data, and the naive implementation
	   can be used for the remaining portion. */
	const int32_t vectorizable_bytes = blocksize - (blocksize % vectorized_chunk_size);
	const int32_t vectorizable_elements = vectorizable_bytes / bytesoftype;
	const int32_t total_elements = blocksize / bytesoftype;

	/* If the block size is too small to be vectorized,
	   use the generic implementation. */
	if (blocksize < vectorized_chunk_size) {
		unshuffle_generic(bytesoftype, blocksize, _src, _dest);
		return;
	}

	/* Optimized unshuffle implementations */
	switch (bytesoftype) {
		case 2:
			unshuffle2_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 4:
			unshuffle4_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 8:
			unshuffle8_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 12:
			unshuffle12_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		case 16:
			unshuffle16_sse2(_dest, _src, vectorizable_elements, total_elements);
			break;
		default:
			if (bytesoftype > (int32_t)sizeof(__m128i)) {
				unshuffle16_tiled_sse2(_dest, _src, vectorizable_elements, total_elements, bytesoftype);
			}
			else {
				/* Non-optimized unshuffle */
				unshuffle_generic(bytesoftype, blocksize, _src, _dest);
				/* The non-optimized function covers the whole buffer,
				   so we're done processing here. */
				return;
			}
	}

	/* If the buffer had any bytes at the end which couldn't be handled
	   by the vectorized implementations, use the non-optimized version
	   to finish them up. */
	if (vectorizable_bytes < blocksize) {
		unshuffle_generic_inline(bytesoftype, vectorizable_bytes, blocksize, _src, _dest);
	}
}

// const bool is_shuffle_sse2 = true;

#else /* defined(__SSE2__) */

// const bool is_shuffle_sse2 = false;

void shuffle_sse2(const int32_t bytesoftype, const int32_t blocksize, const uint8_t* _src, uint8_t* _dest)
{
	abort();
}

void unshuffle_sse2(const int32_t bytesoftype, const int32_t blocksize, const uint8_t* _src, uint8_t* _dest)
{
	abort();
}

#endif /* defined(__SSE2__) */

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
