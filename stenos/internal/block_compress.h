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

#ifndef STENOS_BLOCK_COMPRESS_H
#define STENOS_BLOCK_COMPRESS_H

// #define STENOS_STRONG_DEBUG

#include <utility>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>
#include <stdexcept>

#include "../timer.hpp"
#include "../stenos.h"
#include "simd.h"
#include "shuffle.h"
#include "tiny_pool.h"
#include "lz4dry.h"
#include "lz_compress.h"
#include "zstd_wrapper.h"

#ifdef min
#undef min
#undef max
#endif

// Block header values
#define __STENOS_BLOCK_ALL_SAME 0   // 256 equal elements
#define __STENOS_BLOCK_ALL_RAW 1    // Raw block of 256 elements
#define __STENOS_BLOCK_NORMAL 2	    // Normal compressed block
#define __STENOS_BLOCK_NORMAL_RLE 3 // Normal compressed block with rle mins

// Impossible header values for standard blocks
#define __STENOS_BLOCK_COPY 252	   // Block copied with memcpy
#define __STENOS_BLOCK_LZ 253	   // Block encoded with lz-like algorithm
#define __STENOS_BLOCK_PARTIAL 254 // Partial block for remaining values

// Compression methods
#define __STENOS_COMP_NORMAL 0
#define __STENOS_COMP_RLE 1


namespace stenos
{
	namespace detail
	{
		// Overalign buffer to 16 bytes
		static STENOS_ALWAYS_INLINE void* align_buffer(void* buf) noexcept
		{
			if ((uintptr_t)buf & 15u)
				buf = (void*)(((uintptr_t)buf & (uintptr_t)(~15ull)) + (uintptr_t)16ull);
			return buf;
		}

		// Usefull union, 16 byte aligned block of 16 bytes
		union alignas(16) vector16
		{
			char i8[16];
			uint8_t u8[16];
			uint16_t u16[8];
			uint32_t u32[4];
			uint64_t u64[2];
		};
	}
}

#ifdef __SSE3__

// RLE compression requires SSE3

#if defined(STENOS_WIDE_TABLE)

// Use WIDE tables for RLE encoding/decoding.
// Each table is 1048576 bytes.

#include "shuffle_table.h"
#include "unshuffle_table.h"
namespace stenos
{
	namespace detail
	{
		static STENOS_ALWAYS_INLINE __m128i shuffle_mask(uint16_t mask) noexcept
		{
			return _mm_load_si128(((const __m128i*)get_shuffle_table()) + mask);
		}
		static STENOS_ALWAYS_INLINE __m128i unshuffle_mask(uint16_t mask) noexcept
		{
			return _mm_load_si128(((const __m128i*)get_unshuffle_table()) + mask);
		}
	}
}
#else

// Use redcued tables for RLE encoding/decoding (default).
// Each table is 2048 bytes.

#include "reduced_shuffle_table.h"
#include "reduced_unshuffle_table.h"

namespace stenos
{

	static STENOS_ALWAYS_INLINE int has_error(size_t code)
	{
		return code >= STENOS_LAST_ERROR_CODE;
	}

	namespace detail
	{
		/* static inline const uint64_t* compute_shuffle_table()
		{
			// Compute the reduced table from the full one
			static uint64_t table[256];
			memset(table, 0, sizeof(uint64_t) * 256);
			for (size_t i = 0; i < 256; ++i) {
				uint64_t cnt = popcnt8(~(uint8_t)i);
				uint64_t mask = cnt == 8 ? std::numeric_limits<uint64_t>::max() : ((1ull << (cnt * 8u)) - 1);
				table[i] = reinterpret_cast<const vector16*>(get_shuffle_table())[i].u64[0] & mask;
			}
			return table;
		}*/
		static STENOS_ALWAYS_INLINE const uint64_t* reduced_suffle_table(uint8_t m) noexcept
		{
			static const uint64_t* table = get_reduced_shuffle_table();
			return table + m;
		}
		static STENOS_ALWAYS_INLINE __m128i suffle_table_8(uint8_t m) noexcept
		{
			return _mm_set_epi64x(0, *reduced_suffle_table(m));
		}
		static STENOS_ALWAYS_INLINE __m128i suffle_table_8_shift(uint8_t m, uint32_t shift) noexcept
		{
			uint64_t tmp[3] = { 0, *reduced_suffle_table(m) + 0x0808080808080808ull, 0 };
			return _mm_loadu_si128((const __m128i*)(((char*)&tmp[1]) - shift));
		}
		static STENOS_ALWAYS_INLINE __m128i shuffle_mask(uint16_t mask) noexcept
		{
			// Build shuffle mask by conbining 2 8 bits masks
			uint8_t l = (uint8_t)(mask);
			uint8_t r = (uint8_t)(mask >> 8);
			__m128i m1 = suffle_table_8(l);
			if (r != 255) {
				__m128i right = suffle_table_8_shift(r, popcnt8((uint8_t)~l));
				m1 = _mm_or_si128(m1, right);
			}
			return m1;
		}

		/* static inline const uint64_t* compute_unshuffle_table()
		{
			// Compute the reduced table from the full one
			static uint64_t table[256];
			memset(table, 0, sizeof(uint64_t) * 256);
			for (size_t i = 0; i < 256; ++i) {
				const vector16& v = reinterpret_cast<const vector16*>(get_unshuffle_table())[i];
				table[i] = v.u64[0];
			}
			return table;
		}*/
		static STENOS_ALWAYS_INLINE const uint64_t* reduced_unsuffle_table(uint8_t m) noexcept
		{
			static const uint64_t* table = get_reduced_unshuffle_table();
			return table + m;
		}
		static STENOS_ALWAYS_INLINE __m128i unsuffle_table_8(uint8_t m, uint8_t* last) noexcept
		{
			const uint64_t* v = reduced_unsuffle_table(m);
			*last = *v >> (64ull - 8ull);
			return _mm_set_epi64x(0, *v);
		}
		static STENOS_ALWAYS_INLINE __m128i unsuffle_table_8(uint8_t m) noexcept
		{
			return _mm_set_epi64x(0, *reduced_unsuffle_table(m));
		}
		static STENOS_ALWAYS_INLINE __m128i unshuffle_mask(uint16_t mask) noexcept
		{
			// Build unshuffle table from 2 * 8 bits masks
			uint8_t l = (uint8_t)(mask);
			uint8_t r = (uint8_t)(mask >> 8);
			uint8_t last;
			__m128i unshuffle = unsuffle_table_8(l, &last);
			__m128i right = unsuffle_table_8(r);
			last += ((mask >> 8) & 1) == 0;
			right = _mm_add_epi8(right, _mm_set1_epi8(last));
			right = _mm_slli_si128(right, 8);
			return _mm_or_si128(unshuffle, right);
		}
	}
}
#endif

namespace stenos
{
	namespace detail
	{
		
		static STENOS_ALWAYS_INLINE __m128i from_vector16(const vector16& v) noexcept
		{
			// For reading only, we consider that v is not guaranteed to be aligned
			return _mm_loadu_si128((const __m128i*)&v);
		}
		static STENOS_ALWAYS_INLINE void to_vector16(vector16& v, const __m128i& sse) noexcept
		{
			_mm_store_si128((__m128i*)(&v), sse);
		}
		using vector256 = vector16[16];

		// Structure used for block compression
		struct PackBits
		{
			vector16 _mins, types, _bits, use_rle, rle_sizes, sizes, use_delta_rle, headers, rle_pop_cnt, delta_rle_count;
			uint16_t rle_masks[16];
			uint16_t mins_rle_mask;
			uint16_t delta_rle_mask[16];
			uint16_t size;
			uint8_t mins_rle_count;
			uint8_t all_type;
			uint8_t zeros;
		};

		// Compute rle mask and size for given row
		static STENOS_ALWAYS_INLINE void compute_rle_row_single(const __m128i row, uint16_t* mask, uint8_t* count) noexcept
		{
			__m128i shift = _mm_slli_si128(row, 1);
			__m128i diff = _mm_sub_epi8(row, shift);
			diff = _mm_cmpeq_epi8(diff, _mm_setzero_si128());
			*mask = static_cast<uint16_t>(_mm_movemask_epi8(diff));
			*count = static_cast<uint8_t>(popcnt16(~(*mask)));
		}

		// Write rle encoded row (compute_rle_row musty be called before)
		static STENOS_ALWAYS_INLINE uint8_t* write_rle_single(uint16_t mask, uint8_t count, uint8_t* dst, __m128i row) noexcept
		{
			__m128i values = _mm_shuffle_epi8(row, shuffle_mask(mask));
			write_LE_16(dst, mask);
			dst += 2;
			_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), values);
			return dst + count;
		}

		// Compute rle mask and size for given row
		static STENOS_ALWAYS_INLINE void compute_rle_row(PackBits* p, uint32_t index, __m128i row, __m128i prev_row) noexcept
		{
			__m128i shift = _mm_or_si128(_mm_slli_si128(row, 1), _mm_srli_si128(prev_row, 15));
			__m128i diff = _mm_sub_epi8(row, shift);
			diff = _mm_cmpeq_epi8(diff, _mm_setzero_si128());
			p->rle_masks[index] = static_cast<uint16_t>(_mm_movemask_epi8(diff));
			p->rle_pop_cnt.u8[index] = static_cast<uint8_t>(16 - popcnt16(p->rle_masks[index]));
		}

		// Write rle encoded row (compute_rle_row musty be called before)
		static STENOS_ALWAYS_INLINE uint8_t* write_rle(PackBits* p, uint8_t* dst, uint32_t i, __m128i row) noexcept
		{
			return write_rle_single(p->rle_masks[i], p->rle_pop_cnt.u8[i], dst, row);
		}

		// Write delta rle encoded row (compute_rle_row must be called before)
		template<bool First>
		static STENOS_ALWAYS_INLINE uint8_t* write_delta_rle(PackBits* p, __m128i mask, uint8_t* STENOS_RESTRICT dst, uint32_t i, const vector16* STENOS_RESTRICT src) noexcept
		{
			__m128i row = from_vector16(src[i]);
			__m128i prev = _mm_slli_si128(row, 1);
			if (!First)
				prev = _mm_or_si128(prev, _mm_srli_si128(from_vector16(src[i - 1]), 15));
			row = _mm_sub_epi8(row, prev);
			return write_rle_single(p->delta_rle_mask[i], p->delta_rle_count.u8[i], dst, row);
		}

	}
}

#endif

#ifdef __SSE4_1__

namespace stenos
{

	namespace detail
	{

		

		struct BlockEncoder
		{
			vector256* arrays;    // Arrays for each byte of type (256*BPP bytes)
			void* partial_buffer; // Buffer of size 256*BPP for partial block compression
			PackBits* packs;
			char* firsts; // first value for each bytesoftype

			// Initialize block encoder for given bytesoftype
			STENOS_ALWAYS_INLINE void init(void* buffer, size_t bytesoftype) noexcept
			{
				// Align buffer on 16 bytes
				buffer = align_buffer(buffer);
				this->arrays = reinterpret_cast<vector256*>(buffer);
				this->partial_buffer = static_cast<char*>(buffer) + 256 * bytesoftype;
				this->packs = reinterpret_cast<PackBits*>(static_cast<char*>(buffer) + 256 * bytesoftype + 256);
				this->firsts = (static_cast<char*>(buffer) + 256 * bytesoftype + 256 + sizeof(PackBits) * bytesoftype);
			}
		};

		// Size of internal compression buffer for given bytesoftype
		static STENOS_ALWAYS_INLINE size_t compression_buffer_size(size_t bytesoftype) noexcept
		{
			return 256 * bytesoftype + 256 + sizeof(PackBits) * bytesoftype + bytesoftype + 16; // Add 16 for alignment
		}

		// Bit scan reverse on 2 * 16 bytes
		static STENOS_ALWAYS_INLINE void bit_scan_reverse_8_2(__m128i v1, __m128i v2, __m128i* r1, __m128i* r2) noexcept
		{
			const __m128i lut_lo = _mm_set_epi8(4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8);
			// const __m128i lut_hi = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 8);
			// custom version that convert output 7 to 8, in order to reserve headers 7 and 15 for RLE and RAW
			const __m128i lut_hi = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 3, 8);

			__m128i t = _mm_and_si128(_mm_srli_epi16(v1, 4), _mm_set1_epi8(0x0F));
			t = _mm_shuffle_epi8(lut_hi, t);
			v1 = _mm_shuffle_epi8(lut_lo, v1);
			v1 = _mm_min_epu8(v1, t);
			*r1 = _mm_sub_epi8(_mm_set1_epi8(8), v1);

			t = _mm_and_si128(_mm_srli_epi16(v2, 4), _mm_set1_epi8(0x0F));
			t = _mm_shuffle_epi8(lut_hi, t);
			v2 = _mm_shuffle_epi8(lut_lo, v2);
			v2 = _mm_min_epu8(v2, t);
			*r2 = _mm_sub_epi8(_mm_set1_epi8(8), v2);
		}

		// Horizontal sum of 16 bytes
		static STENOS_ALWAYS_INLINE uint32_t hsum_epu8(__m128i v) noexcept
		{
			__m128i vsum = _mm_sad_epu8(v, _mm_setzero_si128());
			return static_cast<uint32_t>(_mm_extract_epi16(vsum, 0) + _mm_extract_epi16(vsum, 4));
		}

		// Multiplication of 16 bytes
		static STENOS_ALWAYS_INLINE __m128i mullo_epi8(__m128i a, __m128i b) noexcept
		{
			// https://stackoverflow.com/questions/8193601/sse-multiplication-16-x-uint8-t

			// unpack and multiply
			__m128i dst_even = _mm_mullo_epi16(a, b);
			__m128i dst_odd = _mm_mullo_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(b, 8));
			// repack
#ifdef __AVX2__
			// only faster if have access to VPBROADCASTW
			return _mm_or_si128(_mm_slli_epi16(dst_odd, 8), _mm_and_si128(dst_even, _mm_set1_epi16(0xFF)));
#else
			return _mm_or_si128(_mm_slli_epi16(dst_odd, 8), _mm_srli_epi16(_mm_slli_epi16(dst_even, 8), 8));
#endif
		}

		static STENOS_ALWAYS_INLINE __m128i replace(__m128i row, char val, char new_val) noexcept
		{
			__m128i mask = _mm_cmpeq_epi8(row, _mm_set1_epi8(val));
			return _mm_or_si128(_mm_and_si128(mask, _mm_set1_epi8(new_val)), _mm_andnot_si128(mask, row));
		}

		// Process block of 256 elements and find the best compression method for each row of 16 elements: bit packing, delta coding or rle.
		static STENOS_ALWAYS_INLINE uint32_t
		find_pack_bits_params(const vector16* STENOS_RESTRICT src, __m128i* STENOS_RESTRICT trs, uint8_t first, PackBits* STENOS_RESTRICT pack, int methods) noexcept
		{
			const bool has_rle = methods & __STENOS_COMP_RLE;

			__m128i sub, min_sub, max_sub, t0, bits0, bits1;
			__m128i tr_row = trs[0]; // from_vector16(trs[0]);
			__m128i max = tr_row, min = max;
			__m128i first_val = _mm_set1_epi8(static_cast<char>(first));
			__m128i tr_prev = tr_row;

			bool all_same = _mm_movemask_epi8(_mm_cmpeq_epi32(tr_row, first_val)) == 0xFFFF;
			pack->all_type = __STENOS_BLOCK_NORMAL;

			__m128i start = _mm_slli_si128((trs[15]), 1); // initialize to last row shifted right by one
			min_sub = _mm_sub_epi8(tr_row, start);
			max_sub = min_sub;

			for (int i = 1; i < 16; ++i) {
				tr_row = (trs[i]);
				if (all_same)
					all_same &= (_mm_movemask_epi8(_mm_cmpeq_epi32(tr_row, first_val)) == 0xFFFF);
				min = _mm_min_epi8(min, tr_row);
				max = _mm_max_epi8(max, tr_row);
				sub = _mm_sub_epi8(tr_row, tr_prev);
				min_sub = _mm_min_epi8(min_sub, sub);
				max_sub = _mm_max_epi8(max_sub, sub);
				tr_prev = tr_row;
			}

			if (all_same) {
				pack->all_type = __STENOS_BLOCK_ALL_SAME;
				return pack->size = 1;
			}

			bit_scan_reverse_8_2(_mm_sub_epi8(max, min), _mm_sub_epi8(max_sub, min_sub), &bits0, &bits1);
			// Replace 6 by 8 in bits0 to reserve header 6 for delta rle
			bits0 = replace(bits0, 6, 8);
			__m128i bits = _mm_min_epu8(bits0, bits1);
			to_vector16(pack->_bits, bits);

			t0 = _mm_cmpeq_epi8(bits0, bits);
			__m128i types = _mm_andnot_si128(t0, _mm_set1_epi8(1));
			to_vector16(pack->types, types);
			__m128i mins = _mm_or_si128(_mm_and_si128(t0, min), _mm_andnot_si128(t0, min_sub));
			to_vector16(pack->_mins, mins);

			// compute size
			__m128i sizes = mullo_epi8(bits, _mm_set1_epi8(2));
			__m128i add = _mm_andnot_si128(_mm_cmpeq_epi8(bits, _mm_set1_epi8(8)), _mm_set1_epi8(1));
			sizes = _mm_add_epi8(sizes, add);

			__m128i use_rle = _mm_setzero_si128(), use_delta_rle = _mm_setzero_si128(), all_rle = _mm_setzero_si128();
			// Apply rle
			if (has_rle) {
				__m128i prev, row, deltas;
				prev = _mm_setzero_si128();
				STENOS_PREFETCH(src);

				row = from_vector16(src[0]);
				compute_rle_row(pack, 0, row, prev);
				prev = row;

				// delta rle
				deltas = _mm_sub_epi8(row, _mm_slli_si128(row, 1));
				compute_rle_row_single(deltas, &pack->delta_rle_mask[0], &pack->delta_rle_count.u8[0]);

				for (uint32_t i = 1; i < 16; ++i) {
					row = from_vector16(src[i]);
					compute_rle_row(pack, i, row, prev);

					// delta rle
					deltas = _mm_sub_epi8(row, _mm_or_si128(_mm_slli_si128(row, 1), _mm_srli_si128(prev, 15)));
					compute_rle_row_single(deltas, &pack->delta_rle_mask[i], &pack->delta_rle_count.u8[i]);

					prev = row;
				}

				// rle size
				__m128i rle_size = _mm_add_epi8(from_vector16(pack->rle_pop_cnt), _mm_set1_epi8(2));
				use_rle = _mm_cmplt_epi8(rle_size, sizes);
				// take the minimum of rle and bit packing
				sizes = _mm_min_epi8(sizes, rle_size);

				// delta rle size
				rle_size = _mm_add_epi8(from_vector16(pack->delta_rle_count), _mm_set1_epi8(2));
				use_delta_rle = _mm_cmplt_epi8(rle_size, sizes);
				sizes = _mm_min_epi8(sizes, rle_size);

				all_rle = _mm_or_si128(use_rle, use_delta_rle);

				pack->size = hsum_epu8(sizes) + 8u;

				// TEST
				/* if (pack->size > 256 - 8) {
					pack->all_type = __STENOS_BLOCK_ALL_RAW;
					return pack->size;
				}*/

				// Encode mins with rle

				auto count_rle = popcnt16((uint16_t)((short)_mm_movemask_epi8(all_rle)));
				auto bits_8 = _mm_cmpeq_epi8(_mm_andnot_si128(all_rle, bits), _mm_set1_epi8(8));
				auto count8 = popcnt16((uint16_t)((short)_mm_movemask_epi8(bits_8))) + count_rle;
				compute_rle_row_single(mins, &pack->mins_rle_mask, &pack->mins_rle_count);
				auto mins_rle_size = pack->mins_rle_count + 2u;
				if (mins_rle_size < (16u - count8)) {
					pack->all_type = __STENOS_BLOCK_NORMAL_RLE;
					pack->size -= (16u - count8) - mins_rle_size;
					// remove 1 to all sizes except those with 8 bits and rle
					sizes = _mm_sub_epi8(sizes, _mm_andnot_si128(_mm_or_si128(bits_8, all_rle), _mm_set1_epi8(1)));
				}
			}
			else
				pack->size = hsum_epu8(sizes) + 8;

			// TEST
			/* if (pack->size > 256 - 8) {
				pack->all_type = __STENOS_BLOCK_ALL_RAW;
				return pack->size;
			}*/

			{
				// Compute headers
				__m128i rle_headers = _mm_and_si128(_mm_andnot_si128(use_delta_rle, use_rle), _mm_set1_epi8(7));
				rle_headers = _mm_or_si128(rle_headers, _mm_and_si128(use_delta_rle, _mm_set1_epi8(6)));
				bits0 = replace(bits0, 8, 15);
				bits1 = replace(bits1, 8, 7);
				__m128i headers = _mm_or_si128(_mm_and_si128(t0, bits0), _mm_andnot_si128(t0, _mm_add_epi8(bits1, _mm_set1_epi8(8))));
				headers = _mm_or_si128(rle_headers, _mm_andnot_si128(all_rle, headers));
				to_vector16(pack->headers, headers);

#ifdef STENOS_STRONG_DEBUG
				// Check header values
				to_vector16(pack->use_rle, use_rle);
				to_vector16(pack->use_delta_rle, use_delta_rle);
				vector16 true_headers;
				for (int i = 0; i < 16; ++i) {
					if (pack->use_delta_rle.u8[i])
						true_headers.u8[i] = 6;
					else if (pack->use_rle.u8[i])
						true_headers.u8[i] = 7;
					else {
						if (pack->_bits.u8[i] == 8)
							true_headers.u8[i] = 15;
						else {
							if (pack->types.u8[i])
								true_headers.u8[i] = 8 + pack->_bits.u8[i];
							else
								true_headers.u8[i] = pack->_bits.u8[i];
						}
					}
					STENOS_ASSERT_DEBUG(true_headers.u8[i] == pack->headers.u8[i], "");
				}
#endif
			}

			// stores sizes
			to_vector16(pack->sizes, sizes);

			// return full compressed size
			return pack->size;
		}

		static inline const uint8_t* read_16_bits(const uint8_t* src, const uint8_t* end, uint8_t* out, uint32_t bits) noexcept;

#if defined(__BMI2__) && defined(STENOS_ARCH_64)
		static STENOS_ALWAYS_INLINE uint8_t* write_16_bmi2(const uint8_t* v, uint8_t* dst, uint8_t bits) noexcept
		{
			// Use BMI2 _pext_u64
			static const uint64_t mask[9] = {
				0,
				0x0101010101010101ULL,
				0x0303030303030303ULL,
				0x0707070707070707ULL,
				0x0F0F0F0F0F0F0F0FULL,
				0x1F1F1F1F1F1F1F1FULL,
				0x3F3F3F3F3F3F3F3FULL,
				0x7F7F7F7F7F7F7F7FULL,
				0xFFFFFFFFFFFFFFFFULL,
			};
			uint64_t v1 = _pext_u64(read_LE_64(v), mask[bits]);
			write_LE_64(dst, v1);
			uint64_t v2 = _pext_u64(read_LE_64(v + 8), mask[bits]);
			write_LE_64(dst + bits, v2);
			return dst + bits * 2;
		}
#endif

		static inline uint8_t* write_16(const uint8_t* v, uint8_t* dst, uint8_t bits) noexcept
		{
			// Write 16 values using a fixed bits width
			{
#define _U64(val) static_cast<uint64_t>(val)

				switch (bits) {
					case 1:
						dst[0] = v[0] | (v[1] << 1U) | (v[2] << 2U) | (v[3] << 3U) | (v[4] << 4U) | (v[5] << 5U) | (v[6] << 6U) | (v[7] << 7U);
						dst[1] = v[8] | (v[9] << 1U) | (v[10] << 2U) | (v[11] << 3U) | (v[12] << 4U) | (v[13] << 5U) | (v[14] << 6U) | (v[15] << 7U);
						break;
					case 2:
						dst[0] = v[0] | (v[1] << 2U) | (v[2] << 4U) | (v[3] << 6U);
						dst[1] = v[4] | (v[5] << 2U) | (v[6] << 4U) | (v[7] << 6U);
						dst[2] = v[8] | (v[9] << 2U) | (v[10] << 4U) | (v[11] << 6U);
						dst[3] = v[12] | (v[13] << 2U) | (v[14] << 4U) | (v[15] << 6U);
						break;
					case 3:
						write_LE_32(dst, (v[0] | (v[1] << 3U) | (v[2] << 6U) | (v[3] << 9U) | (v[4] << 12U) | (v[5] << 15U) | (v[6] << 18U) | (v[7] << 21U)));
						write_LE_32(dst + 3, (v[8] | (v[9] << 3U) | (v[10] << 6U) | (v[11] << 9U) | (v[12] << 12U) | (v[13] << 15U) | (v[14] << 18U) | (v[15] << 21U)));
						break;
					case 4:
						write_LE_32(dst, (v[0] | (v[1] << 4U) | (v[2] << 8U) | (v[3] << 12U) | (v[4] << 16U) | (v[5] << 20U) | (v[6] << 24U) | (v[7] << 28U)));
						write_LE_32(dst + 4, (v[8] | (v[9] << 4U) | (v[10] << 8U) | (v[11] << 12U) | (v[12] << 16U) | (v[13] << 20U) | (v[14] << 24U) | (v[15] << 28U)));
						break;
					default:
						write_LE_64(dst,
							    v[0] | (v[1] << bits) | (v[2] << bits * 2) | (v[3] << bits * 3) | (_U64(v[4]) << bits * 4) | (_U64(v[5]) << bits * 5) |
							      (_U64(v[6]) << bits * 6) | (_U64(v[7]) << bits * 7));
						write_LE_64(dst + bits,
							    v[8] | (_U64(v[9]) << bits) | (_U64(v[10]) << bits * 2) | (_U64(v[11]) << bits * 3) | (_U64(v[12]) << bits * 4) |
							      (_U64(v[13]) << bits * 5) | (_U64(v[14]) << bits * 6) | (_U64(v[15]) << bits * 7));

						break;
				}

#undef _U64
			}

			return dst + bits * 2;
		}

#if defined(__BMI2__) && defined(STENOS_ARCH_64)
		template<bool First>
		static STENOS_ALWAYS_INLINE uint8_t* write_line_bmi2(const vector16* STENOS_RESTRICT src,
								     __m128i mask,
								     uint32_t i,
								     uint8_t* STENOS_RESTRICT dst,
								     PackBits* STENOS_RESTRICT pack) noexcept
		{
			if (pack->_bits.u8[i]) {
				// type 0 and 1
				vector16 t;
				__m128i row = from_vector16(src[i]);
				//__m128i sub = row;
				if (pack->types.i8[i] != 0) {
					__m128i prev = _mm_set1_epi8(First ? 0 : src[i - 1].i8[15]);
					row = _mm_sub_epi8(row, _mm_or_si128(_mm_slli_si128(row, 1), _mm_and_si128(prev, mask)));
				}
				to_vector16(t, _mm_sub_epi8(row, _mm_set1_epi8(pack->_mins.i8[i])));
				dst = write_16_bmi2(&t.u8[0], dst, pack->_bits.u8[i]);
			}
			return dst;
		}
		template<bool First>
		static STENOS_ALWAYS_INLINE uint8_t* write_line_for_type_bmi2(uint8_t type,
									      const vector16* STENOS_RESTRICT src,
									      __m128i mask,
									      uint32_t i,
									      uint8_t* STENOS_RESTRICT dst,
									      PackBits* STENOS_RESTRICT pack) noexcept
		{
			switch (type) {
				case 15:
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), from_vector16(src[i]));
					return dst + 16;
				case 7:
					return write_rle(pack, dst, i, from_vector16(src[i]));
				case 6:
					return write_delta_rle<First>(pack, mask, dst, i, src);
				default:
					return write_line_bmi2<First>(src, mask, i, dst, pack);
			}
		}
#endif

		template<bool First>
		static STENOS_ALWAYS_INLINE uint8_t* write_line(const vector16* STENOS_RESTRICT src, __m128i mask, uint32_t i, uint8_t* STENOS_RESTRICT dst, PackBits* STENOS_RESTRICT pack) noexcept
		{
			if (pack->_bits.u8[i]) {
				// type 0 and 1
				vector16 t;
				__m128i row = from_vector16(src[i]);
				__m128i sub = row;
				if (pack->types.i8[i] != 0) {
					__m128i prev = _mm_set1_epi8(First ? 0 : src[i - 1].i8[15]);
					sub = _mm_sub_epi8(row, _mm_or_si128(_mm_slli_si128(row, 1), _mm_and_si128(prev, mask)));
				}
				to_vector16(t, _mm_sub_epi8(sub, _mm_set1_epi8(pack->_mins.i8[i])));
				dst = write_16(&t.u8[0], dst, pack->_bits.u8[i]);
			}
			return dst;
		}
		template<bool First>
		static STENOS_ALWAYS_INLINE uint8_t* write_line_for_type(uint8_t type,
									 const vector16* STENOS_RESTRICT src,
									 __m128i mask,
									 uint32_t i,
									 uint8_t* STENOS_RESTRICT dst,
									 PackBits* STENOS_RESTRICT pack) noexcept
		{
			switch (type) {
				case 15:
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), from_vector16(src[i]));
					return dst + 16;
				case 7:
					return write_rle(pack, dst, i, from_vector16(src[i]));
				case 6:
					return write_delta_rle<First>(pack, mask, dst, i, src);
				default:
					return write_line<First>(src, mask, i, dst, pack);
			}
		}

		static inline uint8_t* encode_lines(const vector16* STENOS_RESTRICT src, uint8_t first, PackBits* STENOS_RESTRICT pack, uint8_t* STENOS_RESTRICT dst, unsigned lines) noexcept
		{
			// static const uint8_t header_0[2][9] = {
			//	{ 0, 1, 2, 3, 4, 5, 6, 15, 15 }, { 8, 9, 10, 11, 12, 13, 14, 15, 15 } // 15 is for 8 bits
			// };
			if (pack->all_type == __STENOS_BLOCK_ALL_SAME) {

				*dst++ = first;
				return dst;
			}
			const __m128i mask = _mm_setr_epi8(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
			unsigned lines2 = lines & (~1u);

			auto anchor = dst;
			dst += lines2 / 2 + (lines != lines2);
			for (unsigned i = 0; i < lines2; i += 2) {
				uint8_t h1 = pack->headers.u8[i];
				uint8_t h2 = pack->headers.u8[i + 1];
				*anchor++ = h1 | (h2 << 4);
				if (h1 != 6 && h1 != 7 && h1 != 15)
					*dst++ = pack->_mins.u8[i];
				if (h2 != 6 && h2 != 7 && h2 != 15)
					*dst++ = pack->_mins.u8[i + 1];
			}
			if (lines != lines2) {
				uint8_t h = pack->headers.u8[lines2];
				*anchor++ = h;
				if (h != 6 && h != 7 && h != 15)
					*dst++ = pack->_mins.u8[lines2];
			}

#if defined(__BMI2__) && defined(STENOS_ARCH_64)
			if (cpu_features().HAS_BMI2) {
				if (lines)
					dst = write_line_for_type_bmi2<true>(pack->headers.u8[0], src, mask, 0, dst, pack);
				for (unsigned i = 1; i < lines; ++i) {
					uint8_t h = pack->headers.u8[i]; // pack->use_rle.u8[i] ? 7 : header_0[pack->types.u8[i]][pack->_bits.u8[i]];
					dst = write_line_for_type_bmi2<false>(h, src, mask, i, dst, pack);
				}
			}
			else
#endif
			{
				if (lines)
					dst = write_line_for_type<true>(pack->headers.u8[0], src, mask, 0, dst, pack);
				for (unsigned i = 1; i < lines; ++i) {
					uint8_t h = pack->headers.u8[i]; // pack->use_rle.u8[i] ? 7 : header_0[pack->types.u8[i]][pack->_bits.u8[i]];
					dst = write_line_for_type<false>(h, src, mask, i, dst, pack);
				}
			}
			return dst;
		}

		static STENOS_ALWAYS_INLINE uint8_t* encode16x16_generic(const vector16* STENOS_RESTRICT src, uint8_t first, PackBits* STENOS_RESTRICT pack, uint8_t* STENOS_RESTRICT dst) noexcept
		{
			// Note: this function works if (end -dst) > compress_size-16

			// static const uint8_t header_0[2][9] = {
			//	{ 0, 1, 2, 3, 4, 5, 6, 15, 15 }, { 8, 9, 10, 11, 12, 13, 14, 15, 15 } // 15 is for 8 bits
			// };

			if (pack->all_type == __STENOS_BLOCK_ALL_SAME) {
				*dst++ = first;
				return dst;
			}

			static const __m128i mask = _mm_setr_epi8(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

			if (pack->all_type == __STENOS_BLOCK_NORMAL_RLE) {
				// Write headers
#if defined(__BMI2__) && defined(STENOS_ARCH_64)
				if (cpu_features().HAS_BMI2) {
					dst = write_16_bmi2(pack->headers.u8, dst, 4);
				}
				else
#endif
					dst = write_16(pack->headers.u8, dst, 4);

				// Write mins
				dst = write_rle_single(pack->mins_rle_mask, pack->mins_rle_count, dst, _mm_load_si128((const __m128i*)pack->_mins.u8));
			}
			else {
				auto anchor = dst;
				dst += 8;
				for (unsigned i = 0; i < 16; i += 2) {
					uint8_t h1 = pack->headers.u8[i];
					uint8_t h2 = pack->headers.u8[i + 1];
					*anchor++ = h1 | (h2 << 4);
					if (h1 != 6 && h1 != 7 && h1 != 15)
						*dst++ = pack->_mins.u8[i];
					if (h2 != 6 && h2 != 7 && h2 != 15)
						*dst++ = pack->_mins.u8[i + 1];
				}
			}
#if defined(__BMI2__) && defined(STENOS_ARCH_64)
			if (cpu_features().HAS_BMI2) {

				dst = write_line_for_type_bmi2<true>(pack->headers.u8[0], src, mask, 0, dst, pack);
				dst = write_line_for_type_bmi2<false>(pack->headers.u8[1], src, mask, 1, dst, pack);

				// copy headers (by group of 2) and mins to destination
				for (uint32_t i = 2; i < 16; i += 2) {
					dst = write_line_for_type_bmi2<false>(pack->headers.u8[i], src, mask, i, dst, pack);
					dst = write_line_for_type_bmi2<false>(pack->headers.u8[i + 1], src, mask, i + 1, dst, pack);
				}
			}
			else
#endif
			{
				dst = write_line_for_type<true>(pack->headers.u8[0], src, mask, 0, dst, pack);
				dst = write_line_for_type<false>(pack->headers.u8[1], src, mask, 1, dst, pack);

				// copy headers (by group of 2) and mins to destination
				for (uint32_t i = 2; i < 16; i += 2) {
					dst = write_line_for_type<false>(pack->headers.u8[i], src, mask, i, dst, pack);
					dst = write_line_for_type<false>(pack->headers.u8[i + 1], src, mask, i + 1, dst, pack);
				}
			}

			return dst;
		}

		// Transposition
// http://pzemtsov.github.io/2014/10/01/how-to-transpose-a-16x16-matrix.html
// https://github.com/pzemtsov/article-e1-cache/blob/master/sse.h

/** Combine together four fields of 2 bits each, in lower to high order.
 * Used in 128 and 256 bits shuffles and permutations
 * @param n0 constant integer value of size 2 bits (not checked)
 * @param n1 constant integer value of size 2 bits (not checked)
 * @param n2 constant integer value of size 2 bits (not checked)
 * @param n3 constant integer value of size 2 bits (not checked) (guys, was it really so necessary to write these comments?)
 * @return combined 8-bit value where lower 2 bits contain n0 and high 2 bits contain n3 (format used by __mm_shuffle_ps/SHUFPS)
 */
#define combine_4_2bits(n0, n1, n2, n3) (n0 + (n1 << 2) + (n2 << 4) + (n3 << 6))
// ------ General shuffles and permutations

/** shuffles two 128-bit registers according to four 2-bit constants defining positions.
 * @param x   A0    A1    A2    A3    (each element a 32-bit float)
 * @param y   C0    C1    C2    C3    (each element a 32-bit float)
 * @return    A[n0] A[n1] C[n2] C[n3]
 * Note that positions 0, 1 are only filled with data from x, positions 2, 3 only with data from y.
 * Components of a single vector can be shuffled in any order by using this function with x and inself
 * (see __mm_shuffle_ps intrinsic and SHUFPS instruction)
 */
#define _128_shuffle(x, y, n0, n1, n2, n3) _mm_shuffle_ps(x, y, combine_4_2bits(n0, n1, n2, n3))
/** shuffles two 128-bit integer registers according to four 2-bit constants defining positions.
 * @param x   A0    A1    A2    A3    (each element a 32-bit float)
 * @param y   C0    C1    C2    C3    (each element a 32-bit float)
 * @return    A[n0] A[n1] C[n2] C[n3]
 * Note that positions 0, 1 are only filled with data from x, positions 2, 3 only with data from y.
 * Components of a single vector can be shuffled in any order by using this function with x and inself
 * (see __mm_shuffle_ps intrinsic and SHUFPS instruction)
 */
#define _128i_shuffle(x, y, n0, n1, n2, n3) _mm_castps_si128(_128_shuffle(_mm_castsi128_ps(x), _mm_castsi128_ps(y), n0, n1, n2, n3))

		static STENOS_ALWAYS_INLINE void transpose_4x4_dwords(const __m128i& w0, const __m128i& w1, __m128i const& w2, const __m128i& w3, __m128i& r0, __m128i& r1, __m128i& r2, __m128i& r3)
		{
			// 0  1  2  3
			// 4  5  6  7
			// 8  9  10 11
			// 12 13 14 15

			__m128i x0 = _128i_shuffle(w0, w1, 0, 1, 0, 1); // 0 1 4 5
			__m128i x1 = _128i_shuffle(w0, w1, 2, 3, 2, 3); // 2 3 6 7
			__m128i x2 = _128i_shuffle(w2, w3, 0, 1, 0, 1); // 8 9 12 13
			__m128i x3 = _128i_shuffle(w2, w3, 2, 3, 2, 3); // 10 11 14 15

			/* _mm_store_si128(&r0, _128i_shuffle(x0, x2, 0, 2, 0, 2));
			_mm_store_si128(&r1, _128i_shuffle(x0, x2, 1, 3, 1, 3));
			_mm_store_si128(&r2, _128i_shuffle(x1, x3, 0, 2, 0, 2));
			_mm_store_si128(&r3, _128i_shuffle(x1, x3, 1, 3, 1, 3));*/
			r0 = _128i_shuffle(x0, x2, 0, 2, 0, 2);
			r1 = _128i_shuffle(x0, x2, 1, 3, 1, 3);
			r2 = _128i_shuffle(x1, x3, 0, 2, 0, 2);
			r3 = _128i_shuffle(x1, x3, 1, 3, 1, 3);
		}

		STENOS_ALWAYS_INLINE void transpose_16x16(const __m128i* STENOS_RESTRICT in, __m128i* STENOS_RESTRICT out) noexcept
		{
			__m128i w00, w01, w02, w03;
			__m128i w10, w11, w12, w13;
			__m128i w20, w21, w22, w23;
			__m128i w30, w31, w32, w33;

			const __m128i shuffle = _mm_setr_epi8(0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);

			// Use unaligned access as in is not ALWAYS guaranteed to be aligned
#define STENOS_LOAD(x) _mm_loadu_si128(x)
			transpose_4x4_dwords(STENOS_LOAD(in), STENOS_LOAD(in + 1), STENOS_LOAD(in + 2), STENOS_LOAD(in + 3), w00, w01, w02, w03);
			transpose_4x4_dwords(STENOS_LOAD(in + 4), STENOS_LOAD(in + 5), STENOS_LOAD(in + 6), STENOS_LOAD(in + 7), w10, w11, w12, w13);
			transpose_4x4_dwords(STENOS_LOAD(in + 8), STENOS_LOAD(in + 9), STENOS_LOAD(in + 10), STENOS_LOAD(in + 11), w20, w21, w22, w23);
			transpose_4x4_dwords(STENOS_LOAD(in + 12), STENOS_LOAD(in + 13), STENOS_LOAD(in + 14), STENOS_LOAD(in + 15), w30, w31, w32, w33);
#undef STENOS_LOAD			
			w00 = _mm_shuffle_epi8(w00, shuffle); // transpos 4x4
			w01 = _mm_shuffle_epi8(w01, shuffle);
			w02 = _mm_shuffle_epi8(w02, shuffle);
			w03 = _mm_shuffle_epi8(w03, shuffle);
			w10 = _mm_shuffle_epi8(w10, shuffle);
			w11 = _mm_shuffle_epi8(w11, shuffle);
			w12 = _mm_shuffle_epi8(w12, shuffle);
			w13 = _mm_shuffle_epi8(w13, shuffle);
			w20 = _mm_shuffle_epi8(w20, shuffle);
			w21 = _mm_shuffle_epi8(w21, shuffle);
			w22 = _mm_shuffle_epi8(w22, shuffle);
			w23 = _mm_shuffle_epi8(w23, shuffle);
			w30 = _mm_shuffle_epi8(w30, shuffle);
			w31 = _mm_shuffle_epi8(w31, shuffle);
			w32 = _mm_shuffle_epi8(w32, shuffle);
			w33 = _mm_shuffle_epi8(w33, shuffle);
			transpose_4x4_dwords(w00, w10, w20, w30, out[0], out[1], out[2], out[3]);
			transpose_4x4_dwords(w01, w11, w21, w31, out[4], out[5], out[6], out[7]);
			transpose_4x4_dwords(w02, w12, w22, w32, out[8], out[9], out[10], out[11]);
			transpose_4x4_dwords(w03, w13, w23, w33, out[12], out[13], out[14], out[15]);
		}

		static STENOS_ALWAYS_INLINE uint32_t compute_block_generic(BlockEncoder* encoder, const void* src, char first, uint32_t index, int methods, __m128i* tr) noexcept
		{
			transpose_16x16(reinterpret_cast<const __m128i*>(src), tr);
			return find_pack_bits_params((const vector16*)src, tr, static_cast<uint8_t>(first), encoder->packs + index, methods);
		}

	} // end namespace detail

	static inline void* make_compression_buffer(size_t size) noexcept
	{
		// Create buffer for block compression aligned on 16 bytes
		struct CompressedBuffer
		{
			void* buffer;
			size_t size;
			CompressedBuffer()
			  : buffer(nullptr)
			  , size(0)
			{
			}
			~CompressedBuffer()
			{
				if (buffer)
					stenos::aligned_free(buffer);
			}
		};
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
		// Try to reuse thread local buffer if possible
		static thread_local CompressedBuffer buf;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

		if (buf.size < size) {
			if (buf.buffer)
				stenos::aligned_free(buf.buffer);
			buf.buffer = stenos::aligned_malloc(size, 16);
			buf.size = size;
		}
		return buf.buffer;
	}

	static inline size_t block_compress_partial(const void* STENOS_RESTRICT __src,
						    size_t bytesoftype,
						    size_t bytes,
						    void* STENOS_RESTRICT __dst,
						    size_t dst_size,
						    detail::BlockEncoder* STENOS_RESTRICT encoder,
						    __m128i* STENOS_RESTRICT transpose) noexcept
	{
		// copy src to buffer
		uint8_t* buf = (uint8_t*)encoder->partial_buffer;

		const size_t line_size = 16 * bytesoftype;
		const size_t lines = bytes / line_size;
		const size_t header_size = (bytesoftype >> 1) + ((bytesoftype & 1) ? 1 : 0);
		const uint8_t* src = static_cast<const uint8_t*>(__src);
		uint8_t* dst = static_cast<uint8_t*>(__dst);
		uint8_t* dst_end = static_cast<uint8_t*>(__dst) + dst_size;

		if (lines) {

			memcpy(buf, __src, bytes);
			memset(buf + bytes, (char)buf[bytes - 1], 256 * bytesoftype - bytes);

			uint8_t* anchor = dst;
			uint32_t offset = 0;
			dst += header_size;

			// read source transposed
			shuffle(bytesoftype, bytesoftype * 256, buf, (uint8_t*)(encoder->arrays));

			// copy first value for each bytesoftype
			memcpy(encoder->firsts, buf, bytesoftype);

			for (uint32_t i = 0; i < (uint32_t)bytesoftype; ++i) {

				detail::compute_block_generic(encoder, encoder->arrays[i][0].i8, encoder->firsts[i], i, 0, transpose);
				if (encoder->packs[i].all_type == __STENOS_BLOCK_ALL_SAME) {
					if STENOS_UNLIKELY (dst >= dst_end)
						return STENOS_ERROR_DST_OVERFLOW;
					*dst++ = static_cast<uint8_t>(encoder->firsts[i]);
				}
				else {
					uint32_t size = 8;
					for (size_t j = 0; j < lines; ++j)
						size += encoder->packs[i].sizes.u8[j];

					// Add 16 bytes as write_16 might write 8 bytes beyong the end
					if STENOS_UNLIKELY (dst + size + 8 > dst_end)
						return STENOS_ERROR_DST_OVERFLOW;

					dst = detail::encode_lines(encoder->arrays[i], static_cast<uint8_t>(encoder->firsts[i]), encoder->packs + i, dst, (unsigned)lines);
				}
				// store header value
				if (offset == 0)
					*anchor = 0;
				*anchor |= (encoder->packs[i].all_type << offset);
				offset += 4;
				if (offset == 8) {
					++anchor;
					offset = 0;
				}
			}
		}

		size_t remaining = bytes - lines * line_size;
		if (remaining) {
			if STENOS_UNLIKELY (dst + remaining > dst_end)
				return STENOS_ERROR_DST_OVERFLOW;

			memcpy(dst, src + lines * line_size, remaining);
			dst += remaining;
		}
		return dst - (uint8_t*)__dst;
	}

	// Adjust block compression level at runtime based
	// on provided time constraint
	struct FindCLevel
	{
		double denom_bytes = 0;
		double denom_time = 0;
		FindCLevel() noexcept {}
		FindCLevel(const FindCLevel&) noexcept = default;
		FindCLevel& operator=(const FindCLevel&) noexcept = default;
		FindCLevel(size_t total_bytes, uint64_t max_time) noexcept
		  : denom_bytes(1. / total_bytes)
		  , denom_time(1. / max_time)
		{
		}
		int find_clevel(size_t consummed_bytes, TimeConstraint& t) noexcept
		{
#ifdef NDEBUG
			static constexpr unsigned threshold_bytes_per_second = 2000000000; // 2GB/s
#else
			static constexpr unsigned threshold_bytes_per_second = 200000000; // 200MB/s
#endif
			consummed_bytes += t.processed_bytes;
			size_t remaining_bytes = t.total_bytes - (consummed_bytes);
			auto elapsed = t.timer.tock();

			double ratio_bytes = consummed_bytes * denom_bytes;
			double ratio_time = elapsed * denom_time;

			// Check threshold
			if (ratio_time < 0.2) {
				// For the beginning, compress at best level if this is realistic
				const double required_speed = (remaining_bytes / ((t.nanoseconds - elapsed) * 1e-9));
				if (required_speed < threshold_bytes_per_second)
					return 2;
			}

			if (ratio_time < 0.01 || consummed_bytes == 0)
				return 2; // start at maximum level
			if (ratio_time > 0.5) {
				// halfway: start to check for early stop
				// computed expected time for memcpy at 16GB/s
				auto memcpy_time_ns = remaining_bytes / 16;
				if (elapsed + memcpy_time_ns > t.nanoseconds)
					return -2;
			}
			if (ratio_time > ratio_bytes * 3)
				return -1; // memcpy blocks with the possibility to adjust the level
			if (ratio_time > ratio_bytes * 1.8)
				return 0; // minimum compression level
			if (ratio_time > ratio_bytes * 1.4)
				return 1; // simd only compression
			return 2;	  // full compression
		}
	};

#ifdef STENOS_STRONG_DEBUG
	static size_t block_decompress(const void* STENOS_RESTRICT _src, size_t size, size_t bytesoftype, size_t bytes, void* STENOS_RESTRICT _dst) noexcept;
	static size_t block_decompress_sse(const void* STENOS_RESTRICT _src, size_t size, size_t bytesoftype, size_t bytes, void* STENOS_RESTRICT _dst) noexcept;

	static void test_block_decompress(const void* src, size_t size, size_t bytesoftype, size_t bytes, const void* input)
	{
		std::vector<char> dst(bytesoftype * 256);
		auto r = block_decompress(src, size, bytesoftype, bytes, dst.data());
		if (r != size)
			throw std::runtime_error("decompression error");
		if (memcmp(dst.data(), input, bytes) != 0)
			throw std::runtime_error("decompression error");

		memset(dst.data(), 0, dst.size());
		r = block_decompress_sse(src, size, bytesoftype, bytes, dst.data());
		if (r != size)
			throw std::runtime_error("decompression error");
		if (memcmp(dst.data(), input, bytes) != 0)
			throw std::runtime_error("decompression error");
	}
#endif

	static inline unsigned max_histogram(const uint8_t* src, size_t bytes)
	{
		unsigned hist[256];
		memset(hist, 0, sizeof(hist));
		for (size_t i = 0; i < bytes; ++i)
			++hist[src[i]];
		unsigned maxval = hist[0];
		for (size_t i = 1; i < 256; ++i)
			if (hist[i] > maxval)
				maxval = hist[i];
		return maxval;
	}

	static STENOS_ALWAYS_INLINE size_t block_compress(const void* STENOS_RESTRICT __src,
							  size_t bytesoftype,
							  size_t bytes,
							  void* STENOS_RESTRICT __dst,
							  size_t dst_size,
							  int block_level,
							  int full_level,
							  TimeConstraint& t,
							  double* STENOS_RESTRICT target_ratio,
							  const void* STENOS_RESTRICT __shuffled) noexcept
	{
		static const uint32_t diff[3] = { 25, 16, 0 };
		static const int methods[3] = { 0, __STENOS_COMP_RLE, __STENOS_COMP_RLE };
		static constexpr size_t test_fraction = 16; // if target_ratio non null, test on 1/16 of input size

		if STENOS_UNLIKELY (bytes == 0)
			return 0;

		size_t elements = __shuffled ? bytes / bytesoftype : 0;
		const uint8_t* src = static_cast<const uint8_t*>(__src);
		uint8_t* dst = static_cast<uint8_t*>(__dst);
		uint8_t* dst_end = static_cast<uint8_t*>(__dst) + dst_size;
		size_t header_size = (bytesoftype >> 1) + ((bytesoftype & 1) ? 1 : 0);
		if STENOS_UNLIKELY (block_level > 2)
			block_level = 2;
		int level = (int)block_level;
		uint8_t* saved = dst;

		size_t block_size = bytesoftype * 256;
		size_t block_count = block_size == bytes ? 1 : bytes / block_size;
		size_t remaining_bytes = 0;

		FindCLevel clevel;
		if (t.nanoseconds) {
			level = 2;
			clevel = FindCLevel(t.total_bytes, t.nanoseconds);
		}

		void* buff_src = make_compression_buffer(detail::compression_buffer_size(bytesoftype));
		if STENOS_UNLIKELY (!buff_src)
			return STENOS_ERROR_ALLOC;

		detail::BlockEncoder encoder;
		encoder.init(buff_src, bytesoftype);

		uint8_t* anchor = nullptr;
		uint32_t offset = 0;
		uint32_t target = 0;
		size_t full_size = 0;

		//__m128i input[16];
		__m128i transpose[16];

		for (size_t bcount = 0; bcount < block_count; ++bcount, src += block_size) {

#ifdef STENOS_STRONG_DEBUG
			uint8_t* debug_dst = dst;
#endif

			if (t.nanoseconds) {
				if (level != -2) {
					size_t consummed = (size_t)(src - (const uint8_t*)__src);
					if (consummed >= bytes / test_fraction)
						level = clevel.find_clevel((size_t)(src - (const uint8_t*)__src), t);
				}
				if (level < 0) {
					// memcpy block
					if STENOS_UNLIKELY ((size_t)(dst_end - dst) < block_size + 1u)
						return STENOS_ERROR_DST_OVERFLOW;
					*dst++ = __STENOS_BLOCK_COPY;
					memcpy(dst, src, block_size);
					dst += block_size;
					if (level == -2)
						// Finish with memcpy
						t.finish_memcpy.store(true);
					goto handle_marker;
				}
			}

			anchor = dst;
			offset = 0;
			dst += header_size;

			if (!__shuffled)
				// read source transposed
				shuffle(bytesoftype, block_size, src, (uint8_t*)(encoder.arrays));

			// copy first value for each bytesoftype
			memcpy(encoder.firsts, src, bytesoftype);

			full_size = 0;
			target = 256 - diff[level];
			for (uint32_t i = 0; i < (uint32_t)bytesoftype; i++) {

				const void* input_tr = encoder.arrays[i][0].i8;
				if (__shuffled){
					// In this case,  input_tr is not guaranteed to be aligned
					input_tr = (char*)__shuffled + elements * i + bcount * 256;
				}

				uint32_t size = detail::compute_block_generic(&encoder, input_tr, encoder.firsts[i], i, methods[level], transpose);
				if (size > target) {

					encoder.packs[i].all_type = __STENOS_BLOCK_ALL_RAW;
					size = 256;
				}

				full_size += size;
			}

			// Test LZ compression for level 2
			if (level == 2 && bytesoftype % 4 == 0 && full_size * 3 > block_size) {
				uint16_t buffer[256];

				// To avoid dst overflow, we need at least 8 * bytesoftype + 1 bytes above full_size
				if STENOS_UNLIKELY (dst_end > dst + (full_size + bytesoftype * 8u + 2u)) { // add one byte for __STENOS_BLOCK_LZ
					auto* out = anchor;
					*out++ = __STENOS_BLOCK_LZ;
					out = lz_compress_generic((uint8_t*)src, out, bytesoftype, full_size, buffer);
					if (out) {
						dst = out;
						goto handle_marker;
					}
				}
			}

			if STENOS_UNLIKELY (dst + full_size > dst_end)
				return STENOS_ERROR_DST_OVERFLOW;

			for (uint32_t i = 0; i < (uint32_t)bytesoftype; ++i) {
				const void* input_tr = encoder.arrays[i][0].i8;
				if (__shuffled) {
					// In this case,  input_tr is not guaranteed to be aligned
					input_tr = (char*)__shuffled + elements * i + bcount * 256;
				}

				if (encoder.packs[i].all_type == __STENOS_BLOCK_ALL_RAW) {
					memcpy(dst, input_tr, 256);
					dst += 256;
				}
				else {
					// Add at least 16 bytes to take into account RLE writing that might write 15 bytes beyong dst
					if STENOS_UNLIKELY (dst + encoder.packs[i].size + 16 > dst_end)
						return STENOS_ERROR_DST_OVERFLOW;
					dst = detail::encode16x16_generic((const detail::vector16*)input_tr, static_cast<uint8_t>(encoder.firsts[i]), encoder.packs + i, dst);
				}

				// store header value
				if (offset == 0) {
					if STENOS_UNLIKELY (anchor >= dst_end)
						return STENOS_ERROR_DST_OVERFLOW;
					*anchor = 0;
				}
				*anchor |= (encoder.packs[i].all_type << offset);
				offset += 4;
				if (offset == 8) {
					++anchor;
					offset = 0;
				}
			}

		handle_marker:

#ifdef STENOS_STRONG_DEBUG
			test_block_decompress(debug_dst, dst - debug_dst, bytesoftype, bytesoftype * 256, src);
#endif

			// Check target ratio
			if (target_ratio && (size_t)((src + block_size) - (const uint8_t*)__src) >= bytes / test_fraction) {
				double ratio = ((src + block_size) - (const uint8_t*)__src) / (double)(dst - (uint8_t*)__dst);

				if (ratio < *target_ratio && level >= 0) // avoid going through zstd if block compression is too slow (level < 0)
					return STENOS_ERROR_DST_OVERFLOW;

				target_ratio = nullptr;
			}
		}

		remaining_bytes = bytes - (block_count * block_size);
		if (remaining_bytes) {

#ifdef STENOS_STRONG_DEBUG
			auto* debug_dst = dst;
#endif
			// remaining values
			if STENOS_UNLIKELY (dst + 2 > dst_end)
				return STENOS_ERROR_DST_OVERFLOW;

			// Last block is always a partial one
			*dst++ = __STENOS_BLOCK_PARTIAL;

			size_t r = block_compress_partial(src, bytesoftype, remaining_bytes, dst, dst_end - dst, &encoder, transpose);
			if (has_error(r))
				return r;
			dst += r;

#ifdef STENOS_STRONG_DEBUG
			test_block_decompress(debug_dst, dst - debug_dst, bytesoftype, remaining_bytes, src);
#endif
		}

		size_t result = static_cast<size_t>(dst - saved);
		return result;
	}

} // end stenos

#else

// NO SSE4.1 AVAILABLE!!!!

namespace stenos
{
	template<class... Args>
	static STENOS_ALWAYS_INLINE size_t block_compress(Args...) noexcept
	{
		return STENOS_ERROR_INVALID_INSTRUCTION_SET;
	}
}

#endif

namespace stenos
{
	namespace detail
	{

#define _UCH static_cast<uint8_t>

		static inline const uint8_t* read_16_bits_slow(const uint8_t* STENOS_RESTRICT _src, const uint8_t* STENOS_RESTRICT end, uint8_t* STENOS_RESTRICT out, uint32_t bits) noexcept
		{
			const auto* src = _src;
			uint8_t buf[16];
			if (end - _src < 16) {
				memcpy(buf, _src, bits * 2);
				src = buf;
			}

			if (bits == 1) {
				uint32_t tmp = *src;
				out[0] = _UCH(tmp & 0x1U);
				out[1] = _UCH((tmp >> 1U) & 0x1U);
				out[2] = _UCH((tmp >> 2U) & 0x1U);
				out[3] = _UCH((tmp >> 3U) & 0x1U);
				out[4] = _UCH((tmp >> 4U) & 0x1U);
				out[5] = _UCH((tmp >> 5U) & 0x1U);
				out[6] = _UCH((tmp >> 6U) & 0x1U);
				out[7] = _UCH(tmp >> 7U);
				tmp = *++src;
				out += 8;
				out[0] = _UCH(tmp & 0x1U);
				out[1] = _UCH((tmp >> 1U) & 0x1U);
				out[2] = _UCH((tmp >> 2U) & 0x1U);
				out[3] = _UCH((tmp >> 3U) & 0x1U);
				out[4] = _UCH((tmp >> 4U) & 0x1U);
				out[5] = _UCH((tmp >> 5U) & 0x1U);
				out[6] = _UCH((tmp >> 6U) & 0x1U);
				out[7] = _UCH(tmp >> 7U);
			}
			else if (bits == 2) {
				out[0] = _UCH(*src & 0x3);
				out[1] = _UCH((*src >> 2) & 0x3);
				out[2] = _UCH((*src >> 4) & 0x3);
				out[3] = _UCH((*src >> 6));
				out[4] = _UCH(src[1] & 0x3);
				out[5] = _UCH((src[1] >> 2) & 0x3);
				out[6] = _UCH((src[1] >> 4) & 0x3);
				out[7] = _UCH((src[1] >> 6));
				src += 2; // bits;
				out += 8;
				out[0] = _UCH(*src & 0x3);
				out[1] = _UCH((*src >> 2) & 0x3);
				out[2] = _UCH((*src >> 4) & 0x3);
				out[3] = _UCH((*src >> 6));
				out[4] = _UCH(src[1] & 0x3);
				out[5] = _UCH((src[1] >> 2) & 0x3);
				out[6] = _UCH((src[1] >> 4) & 0x3);
				out[7] = _UCH((src[1] >> 6));
			}
			else if (bits == 3) {
				uint32_t r = read_LE_32(src);
				out[0] = _UCH(r & 0x7);
				out[1] = _UCH((r >> 3) & 0x7);
				out[2] = _UCH((r >> 6) & 0x7);
				out[3] = _UCH((r >> 9) & 0x7);
				out[4] = _UCH((r >> 12) & 0x7);
				out[5] = _UCH((r >> 15) & 0x7);
				out[6] = _UCH((r >> 18) & 0x7);
				out[7] = _UCH((r >> 21) & 0x7);
				src += 3; // bits;
				out += 8;
				r = read_LE_32(src);
				out[0] = _UCH(r & 0x7);
				out[1] = _UCH((r >> 3) & 0x7);
				out[2] = _UCH((r >> 6) & 0x7);
				out[3] = _UCH((r >> 9) & 0x7);
				out[4] = _UCH((r >> 12) & 0x7);
				out[5] = _UCH((r >> 15) & 0x7);
				out[6] = _UCH((r >> 18) & 0x7);
				out[7] = _UCH((r >> 21) & 0x7);
			}
			else if (bits == 4) {

				uint32_t r1 = read_LE_32(src);
				src += 4; // bits;
				uint32_t r2 = read_LE_32(src);

				out[0] = _UCH(r1 & 0xFU);
				out[1] = _UCH((r1 >> 4U) & 0xFU);
				out[2] = _UCH((r1 >> 8U) & 0xFU);
				out[3] = _UCH((r1 >> 12U) & 0xFU);
				out[4] = _UCH((r1 >> 16U) & 0xFU);
				out[5] = _UCH((r1 >> 20U) & 0xFU);
				out[6] = _UCH((r1 >> 24U) & 0xFU);
				out[7] = _UCH(r1 >> 28U);
				out[8] = _UCH(r2 & 0xFU);
				out[9] = _UCH((r2 >> 4U) & 0xFU);
				out[10] = _UCH((r2 >> 8U) & 0xFU);
				out[11] = _UCH((r2 >> 12U) & 0xFU);
				out[12] = _UCH((r2 >> 16U) & 0xFU);
				out[13] = _UCH((r2 >> 20U) & 0xFU);
				out[14] = _UCH((r2 >> 24U) & 0xFU);
				out[15] = _UCH(r2 >> 28U);
			}
			else {

				uint64_t r1 = read_LE_64(src);
				src += bits;
				uint64_t r2 = read_LE_64(src);
				uint64_t mask = (1ull << static_cast<uint64_t>(bits)) - 1ull;
				uint64_t _bits = bits;
				out[0] = _UCH(r1 & mask);
				out[1] = _UCH((r1 >> _bits) & mask);
				out[2] = _UCH((r1 >> _bits * 2ULL) & mask);
				out[3] = _UCH((r1 >> _bits * 3ULL) & mask);
				out[4] = _UCH((r1 >> _bits * 4ULL) & mask);
				out[5] = _UCH((r1 >> _bits * 5ULL) & mask);
				out[6] = _UCH((r1 >> _bits * 6ULL) & mask);
				out[7] = _UCH((r1 >> _bits * 7ULL) & mask);
				out[8] = _UCH(r2 & mask);
				out[9] = _UCH((r2 >> _bits) & mask);
				out[10] = _UCH((r2 >> _bits * 2ULL) & mask);
				out[11] = _UCH((r2 >> _bits * 3ULL) & mask);
				out[12] = _UCH((r2 >> _bits * 4ULL) & mask);
				out[13] = _UCH((r2 >> _bits * 5ULL) & mask);
				out[14] = _UCH((r2 >> _bits * 6ULL) & mask);
				out[15] = _UCH((r2 >> _bits * 7ULL) & mask);
			}

			return _src + bits * 2;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* read_16_bits(const uint8_t* STENOS_RESTRICT src, const uint8_t* STENOS_RESTRICT end, uint8_t* STENOS_RESTRICT out, uint32_t bits) noexcept
		{

#if defined(__BMI2__) && defined(STENOS_ARCH_64)
			if (cpu_features().HAS_BMI2) {

				static const uint64_t mask[9] = {
					0,
					0x0101010101010101ULL,
					0x0303030303030303ULL,
					0x0707070707070707ULL,
					0x0F0F0F0F0F0F0F0FULL,
					0x1F1F1F1F1F1F1F1FULL,
					0x3F3F3F3F3F3F3F3FULL,
					0x7F7F7F7F7F7F7F7FULL,
					0xFFFFFFFFFFFFFFFFULL,
				};
				if STENOS_LIKELY (end > src + 15) {
					uint64_t v1 = _pdep_u64(read_LE_64(src), mask[bits]);
					write_LE_64(out, v1);
					uint64_t v2 = _pdep_u64(read_LE_64(src + bits), mask[bits]);
					write_LE_64(out + 8, v2);
				}
				else {
					uint8_t vals[16];
					memcpy(vals, src, (size_t)(end - src));
					uint64_t v1 = _pdep_u64(read_LE_64(vals), mask[bits]);
					write_LE_64(out, v1);
					uint64_t v2 = _pdep_u64(read_LE_64(vals + bits), mask[bits]);
					write_LE_64(out + 8, v2);
				}
				return src + bits * 2;
			}
#endif
			return read_16_bits_slow(src, end, out, bits);
		}

		static STENOS_ALWAYS_INLINE void fast_copy_stridded_16(uint8_t* STENOS_RESTRICT dst,
								       const uint8_t* STENOS_RESTRICT src,
								       uint8_t offset,
								       uint32_t stride,
								       bool check_stride = true) noexcept
		{
			const uint32_t stride2 = stride << 1u;
			*dst = (src ? src[0] : 0) + offset;
			dst[stride] = (src ? src[1] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[2] : 0) + offset;
			dst[stride] = (src ? src[3] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[4] : 0) + offset;
			dst[stride] = (src ? src[5] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[6] : 0) + offset;
			dst[stride] = (src ? src[7] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[8] : 0) + offset;
			dst[stride] = (src ? src[9] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[10] : 0) + offset;
			dst[stride] = (src ? src[11] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[12] : 0) + offset;
			dst[stride] = (src ? src[13] : 0) + offset;
			dst += stride2;
			*dst = (src ? src[14] : 0) + offset;
			dst[stride] = (src ? src[15] : 0) + offset;
		}

		static STENOS_ALWAYS_INLINE void fast_memset_stridded_16(uint8_t* dst, uint8_t val, const uint32_t stride) noexcept
		{
			fast_copy_stridded_16(dst, 0, val, stride, false);
		}

		static STENOS_ALWAYS_INLINE void fast_copyleft_stridded_16(uint8_t* STENOS_RESTRICT dst, const uint8_t* STENOS_RESTRICT src, uint8_t first, uint8_t offset, uint32_t inner) noexcept
		{
			const uint32_t inner2 = inner << 1;
			uint32_t pos = inner;
			dst[0] = (src ? src[0] : 0) + first + offset;
			dst[inner] = (src ? src[1] : 0) + dst[0] + offset;
			dst[pos + inner] = (src ? src[2] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[3] : 0) + dst[pos + inner] + offset;
			pos += inner2;
			dst[pos + inner] = (src ? src[4] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[5] : 0) + dst[pos + inner] + offset;
			pos += inner2;
			dst[pos + inner] = (src ? src[6] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[7] : 0) + dst[pos + inner] + offset;
			pos += inner2;
			dst[pos + inner] = (src ? src[8] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[9] : 0) + dst[pos + inner] + offset;
			pos += inner2;
			dst[pos + inner] = (src ? src[10] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[11] : 0) + dst[pos + inner] + offset;
			pos += inner2;
			dst[pos + inner] = (src ? src[12] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[13] : 0) + dst[pos + inner] + offset;
			pos += inner2;
			dst[pos + inner] = (src ? src[14] : 0) + dst[pos] + offset;
			dst[pos + inner2] = (src ? src[15] : 0) + dst[pos + inner] + offset;
		}

		static inline const uint8_t* decode_raw(const uint8_t* STENOS_RESTRICT src,
							uint8_t* STENOS_RESTRICT dst,
							uint32_t inner_stride,
							uint32_t outer_stride,
							const uint8_t* STENOS_RESTRICT end) noexcept
		{
			// check for src overflow
			if STENOS_UNLIKELY ((end - src) < 256)
				return nullptr;
			for (uint32_t i = 0; i < 16; ++i)
				fast_copy_stridded_16((dst + i * outer_stride), src + i * 16, 0, inner_stride);
			return src + 256;
		}

		static inline const uint8_t* decode_same(const uint8_t* STENOS_RESTRICT src,
							 uint8_t* STENOS_RESTRICT dst,
							 uint32_t inner_stride,
							 uint32_t outer_stride,
							 const uint8_t* STENOS_RESTRICT end,
							 uint32_t lines = 16) noexcept
		{
			// check overflow
			if STENOS_UNLIKELY (src >= end)
				return nullptr;

			// set the block to the same unique value
			auto same = *src++;
			for (unsigned y = 0; y < lines; ++y)
				fast_memset_stridded_16(dst + y * outer_stride, same, inner_stride);
			return src;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* decode_rle(const uint8_t* STENOS_RESTRICT src,
								      const uint8_t* STENOS_RESTRICT end,
								      uint8_t* STENOS_RESTRICT dst,
								      uint8_t prev,
								      uint32_t inner) noexcept
		{
			if STENOS_UNLIKELY (end - src < 2)
				return nullptr;
			uint16_t mask = read_LE_16(src);
			src += 2;
			uint32_t size = popcnt16(~mask);
			uint32_t remaining = static_cast<uint32_t>(end - src);
			if STENOS_UNLIKELY (size > remaining)
				return nullptr;

			// Non SSE variant
			const uint8_t* s[2] = { src, dst };
			*dst = (mask & 1) ? prev : *s[0]++;
			dst += inner;

			for (uint16_t i = 1; i < 16; ++i) {
				const bool is_prev = (bool)((mask >> i) & 1);
				*dst = *s[is_prev];
				s[1] = dst;
				s[0] += !is_prev;
				dst += inner;
			}
			return s[0]; // src;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* decode_line(uint8_t h,
								       const uint8_t* STENOS_RESTRICT src,
								       const uint8_t* STENOS_RESTRICT end,
								       uint8_t* STENOS_RESTRICT dst,
								       uint32_t x,
								       uint32_t inner_stride,
								       uint32_t outer_stride,
								       const uint8_t* STENOS_RESTRICT mins) noexcept
		{
			static const uint32_t _bit_count_0[16] = { 0, 1, 2, 3, 4, 5, 6, 8, 0, 1, 2, 3, 4, 5, 6, 8 };

			if (h == 6) {
				vector16 tmp;
				src = decode_rle(src, end, tmp.u8, 0, 1);
				if STENOS_UNLIKELY (!src)
					return nullptr;
				fast_copyleft_stridded_16(dst + x * outer_stride, tmp.u8, x == 0 ? 0 : dst[(x - 1) * outer_stride + 15 * inner_stride], 0, inner_stride);
			}
			else if (h == 7) {
				// rle
				src = decode_rle(src, end, dst + x * outer_stride, x == 0 ? 0 : dst[(x - 1) * outer_stride + 15 * inner_stride], inner_stride);
				if STENOS_UNLIKELY (!src)
					return nullptr;
			}
			else if (h == 15) {
				// raw row
				// check overflow
				if STENOS_UNLIKELY (end - src < 16)
					return nullptr;
				fast_copy_stridded_16(dst + x * outer_stride, src, 0, inner_stride);
				src += 16;
			}
			else {

				auto cnt = _bit_count_0[h];
				// check overflow
				if STENOS_UNLIKELY (end < src + cnt * 2)
					return nullptr;
				auto min = mins[x];

				if (cnt > 0) {
					// read compressed column
					alignas(16) uint8_t column[16];
					src = read_16_bits(src, end, column, cnt);
					if (h < 8) // type 0
						fast_copy_stridded_16(dst + x * outer_stride, column, min, inner_stride);
					else // type 1
						fast_copyleft_stridded_16(dst + x * outer_stride, column, x == 0 ? 0 : dst[(x - 1) * outer_stride + 15 * inner_stride], min, inner_stride);
				}
				else {
					if (h < 8) // type 0
						fast_memset_stridded_16(dst + x * outer_stride, min, inner_stride);
					else // type 1
						fast_copyleft_stridded_16(dst + x * outer_stride, 0, x == 0 ? 0 : dst[(x - 1) * outer_stride + 15 * inner_stride], min, inner_stride);
				}
			}
			return src;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* decode_block_(const uint8_t* STENOS_RESTRICT src,
									 uint8_t* STENOS_RESTRICT dst,
									 uint32_t inner_stride,
									 uint32_t outer_stride,
									 const uint8_t* STENOS_RESTRICT end,
									 uint32_t lines,
									 const uint8_t* STENOS_RESTRICT headers,
									 const uint8_t* STENOS_RESTRICT mins) noexcept
		{
			// decode rows
			uint32_t lines2 = lines & (~1u);
			for (uint32_t i = 0; i < lines2; i += 2) {
				src = decode_line(headers[i], src, end, dst, i, inner_stride, outer_stride, mins);
				src = decode_line(headers[i + 1], src, end, dst, i + 1, inner_stride, outer_stride, mins);
			}
			if (lines != lines2) {
				src = decode_line(headers[lines2], src, end, dst, lines2, inner_stride, outer_stride, mins);
			}
			return src;
		}
		static inline const uint8_t* decode_block(const uint8_t* STENOS_RESTRICT src,
							  uint8_t* STENOS_RESTRICT dst,
							  uint32_t inner_stride,
							  uint32_t outer_stride,
							  const uint8_t* STENOS_RESTRICT end,
							  uint32_t lines = 16) noexcept
		{
			auto headers_len = lines / 2 + (lines & 1);
			if STENOS_UNLIKELY (src + headers_len + lines > end)
				return nullptr;

			vector16 headers, mins;
			auto lines2 = lines & (~1u);
			auto min_start = src + headers_len;
			for (unsigned i = 0; i < lines2; i += 2, ++src) {
				auto h0 = headers.u8[i] = *src & 0xF;
				auto h1 = headers.u8[i + 1] = *src >> 4;
				if (h0 != 6 && h0 != 7 && h0 != 15)
					mins.u8[i] = *min_start++;
				if (h1 != 6 && h1 != 7 && h1 != 15)
					mins.u8[i + 1] = *min_start++;
			}
			if (lines != lines2) {
				auto h0 = headers.u8[lines2] = *src & 0xF;
				if (h0 != 6 && h0 != 7 && h0 != 15)
					mins.u8[lines2] = *min_start++;
			}

			return decode_block_(min_start, dst, inner_stride, outer_stride, end, lines, headers.u8, mins.u8);
		}
		static inline const uint8_t* decode_block_rle(const uint8_t* STENOS_RESTRICT src,
							      uint8_t* STENOS_RESTRICT dst,
							      uint32_t inner_stride,
							      uint32_t outer_stride,
							      const uint8_t* STENOS_RESTRICT end,
							      uint32_t lines = 16) noexcept
		{
			// read compressed mins
			vector16 headers, mins;
			auto lines2 = lines & (~1u);
			for (unsigned i = 0; i < lines2; i += 2, ++src) {
				headers.u8[i] = *src & 0xF;
				headers.u8[i + 1] = *src >> 4;
			}
			if (lines != lines2)
				headers.u8[lines2] = *src++ & 0xF;

			src = decode_rle(src, end, mins.u8, 0, 1);
			if STENOS_UNLIKELY (!src)
				return nullptr;
			return decode_block_(src, dst, inner_stride, outer_stride, end, lines, headers.u8, mins.u8);
		}

	} // end namespace detail

	static inline size_t block_decompress_partial(const void* STENOS_RESTRICT _src, size_t size, size_t bytesoftype, size_t bytes, void* STENOS_RESTRICT _dst) noexcept
	{
		const uint8_t* src = static_cast<const uint8_t*>(_src);
		const uint8_t* saved = src;
		const uint8_t* end = src + size;
		const uint32_t header_len = (uint32_t)((bytesoftype >> 1) + ((bytesoftype & 1) ? 1 : 0));
		const uint32_t outer_stride = (uint32_t)bytesoftype * 16;
		const uint32_t inner_stride = (uint32_t)bytesoftype;
		const size_t line_size = 16 * bytesoftype;
		const size_t lines = bytes / line_size;
		uint8_t* dst = static_cast<uint8_t*>(_dst);

		if (lines) {

			const uint8_t* anchor = (src);
			src += header_len;
			if STENOS_UNLIKELY (src >= end)
				return STENOS_ERROR_SRC_OVERFLOW;

			for (uint32_t i = 0; i < (uint32_t)bytesoftype; ++i) {

				uint8_t block_header = (anchor[i >> 1] >> (4 * (i & 1))) & 15;

				switch (block_header) {
					case __STENOS_BLOCK_ALL_SAME:
						src = detail::decode_same(src, dst + i, inner_stride, outer_stride, end, (unsigned)lines);
						break;
					case __STENOS_BLOCK_NORMAL:
						src = detail::decode_block(src, dst + i, inner_stride, outer_stride, end, (unsigned)lines);
						break;
					default:
						return STENOS_ERROR_INVALID_INPUT;
				}
				if STENOS_UNLIKELY (!src)
					return STENOS_ERROR_SRC_OVERFLOW;
			}
		}

		size_t remaining = bytes - lines * line_size;
		if (remaining) {
			if STENOS_UNLIKELY (src + remaining > end)
				return STENOS_ERROR_SRC_OVERFLOW;
			memcpy(dst + lines * line_size, src, remaining);
			src += remaining;
		}
		return src - saved;
	}

	static STENOS_ALWAYS_INLINE size_t block_decompress(const void* STENOS_RESTRICT _src, size_t size, size_t bytesoftype, size_t bytes, void* STENOS_RESTRICT _dst) noexcept
	{
		if STENOS_UNLIKELY (bytes == 0 || size == 0)
			return 0;

		const uint8_t* src = static_cast<const uint8_t*>(_src);
		const uint8_t* saved = src;
		const uint8_t* end = src + size;
		const uint32_t header_len = (uint32_t)((bytesoftype >> 1) + ((bytesoftype & 1) ? 1 : 0));
		uint8_t* dst = static_cast<uint8_t*>(_dst);
		uint32_t outer_stride = (uint32_t)bytesoftype * 16;
		uint32_t inner_stride = (uint32_t)bytesoftype;

		size_t block_size = bytesoftype * 256;
		size_t block_count = bytes == block_size ? 1 : bytes / block_size;

		// check for minimum size
		if STENOS_UNLIKELY (size < (header_len + bytesoftype) && block_count)
			return STENOS_ERROR_SRC_OVERFLOW;

		for (size_t bcount = 0; bcount < block_count; ++bcount, dst += block_size) {
			const uint8_t* anchor = (src);
			src += header_len;
			if STENOS_UNLIKELY (src >= end)
				return STENOS_ERROR_SRC_OVERFLOW;

			if (*anchor == __STENOS_BLOCK_COPY) {
				src = anchor + 1;
				memcpy(dst, src, block_size);
				src += block_size;
				continue;
			}
			if (*anchor == __STENOS_BLOCK_LZ) {
				src = anchor + 1;
				src = lz_decompress_generic(src, dst, bytesoftype, (size_t)(end - src));
				if (!src)
					return STENOS_ERROR_INVALID_INPUT;
				continue;
			}

			for (uint32_t i = 0; i < (uint32_t)bytesoftype; ++i) {

				uint8_t block_header = (anchor[i >> 1] >> (4 * (i & 1))) & 15;

				switch (block_header) {
					case __STENOS_BLOCK_ALL_RAW:
						src = detail::decode_raw(src, dst + i, inner_stride, outer_stride, end);
						break;
					case __STENOS_BLOCK_ALL_SAME:
						src = detail::decode_same(src, dst + i, inner_stride, outer_stride, end);
						break;
					case __STENOS_BLOCK_NORMAL:
						src = detail::decode_block(src, dst + i, inner_stride, outer_stride, end);
						break;
					case __STENOS_BLOCK_NORMAL_RLE:
						src = detail::decode_block_rle(src, dst + i, inner_stride, outer_stride, end);
						break;
					default:
						return STENOS_ERROR_INVALID_INPUT;
				}
				if STENOS_UNLIKELY (!src)
					return STENOS_ERROR_SRC_OVERFLOW;
			}
		}

		size_t remaining_bytes = bytes - (block_size * block_count);
		if (remaining_bytes) {
			if (src == end)
				return STENOS_ERROR_SRC_OVERFLOW;

			uint32_t code = *src++;

			if STENOS_UNLIKELY (code != __STENOS_BLOCK_PARTIAL)
				return STENOS_ERROR_INVALID_INPUT;

			auto r = block_decompress_partial(src, end - src, bytesoftype, bytes - block_size * block_count, (uint8_t*)(_dst) + block_size * block_count);
			if STENOS_UNLIKELY (has_error(r))
				return r;
			src += r;
		}

		return static_cast<size_t>(src - saved);
	}

#ifdef __SSE3__

	// Faster decoding if SSE3 is available

	namespace detail
	{

		static STENOS_ALWAYS_INLINE void fast_offset_flat(uint8_t* dst, uint8_t offset) noexcept
		{
			if (offset) {
				__m128i v = _mm_loadu_si128((const __m128i*)dst);
				v = _mm_add_epi8(v, _mm_set1_epi8(offset));
				_mm_storeu_si128((__m128i*)dst, v);
			}
		}

		static STENOS_ALWAYS_INLINE __m128i prefix_sum_16(__m128i x) noexcept
		{
			x = _mm_add_epi8(x, _mm_slli_si128(x, 1));
			x = _mm_add_epi8(x, _mm_slli_si128(x, 2));
			x = _mm_add_epi8(x, _mm_slli_si128(x, 4));
			x = _mm_add_epi8(x, _mm_slli_si128(x, 8));
			return x;
		}

		static STENOS_ALWAYS_INLINE void fast_copyleft_flat(uint8_t* STENOS_RESTRICT dst, const uint8_t* STENOS_RESTRICT src, uint8_t first, uint8_t offset) noexcept
		{
			__m128i row = _mm_setzero_si128();
			if (src)
				row = _mm_load_si128((const __m128i*)src);
			row = _mm_add_epi8(row, _mm_set1_epi8(offset));
			__m128i _first = _mm_srli_si128(_mm_set1_epi8(first), 15);
			row = _mm_add_epi8(row, _first);
			_mm_store_si128((__m128i*)dst, prefix_sum_16(row));
		}

		static inline const uint8_t* decode_raw_flat(const uint8_t* STENOS_RESTRICT src, uint8_t* STENOS_RESTRICT dst, const uint8_t* STENOS_RESTRICT end) noexcept
		{
			// check for src overflow
			if STENOS_UNLIKELY ((end - src) < 256)
				return nullptr;

			memcpy(dst, src, 256);
			return src + 256;
		}

		static inline const uint8_t* decode_same_flat(const uint8_t* STENOS_RESTRICT src, uint8_t* STENOS_RESTRICT dst, const uint8_t* end) noexcept
		{
			// check overflow
			if STENOS_UNLIKELY (src >= end)
				return nullptr;

			// set the block to the same unique value
			auto same = *src++;
			memset(dst, (int)same, 256);
			return src;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* decode_rle_flat(const uint8_t* STENOS_RESTRICT src, const uint8_t* STENOS_RESTRICT end, uint8_t* STENOS_RESTRICT dst, uint8_t prev) noexcept
		{
			if STENOS_UNLIKELY (end - src < 2)
				return nullptr;
			uint16_t mask = read_LE_16(src);
			src += 2;
			uint32_t size = popcnt16(~mask);
			uint32_t remaining = static_cast<uint32_t>(end - src);
			if STENOS_UNLIKELY (size > remaining)
				return nullptr;

			// We need to read the src into a __m128i register, and insert previous value
			// to the index 0 while shifting the rest to the right by 1 byte (if repetition
			// from previous row).
			// The version below is faster than using a bunch of intrinsics.
			vector16 buff;
			uint8_t has_first;
			if STENOS_UNLIKELY (remaining < 16) {
				has_first = (mask & 1);
				memcpy(&buff, src - (mask & 1), remaining + 1);
			}
			else {
				has_first = (mask & 1);
				memcpy(&buff, src - (mask & 1), 16);
			}
			buff.u8[0] = has_first ? prev : buff.u8[0];
			// direct store to dst
			_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_shuffle_epi8(from_vector16(buff), unshuffle_mask(mask)));
			return src + size;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* decolde_line_flat(uint8_t h,
									     const uint8_t* STENOS_RESTRICT src,
									     const uint8_t* STENOS_RESTRICT end,
									     uint8_t* STENOS_RESTRICT dst,
									     uint32_t x,
									     const uint8_t* mins) noexcept
		{
			static const uint8_t _bit_count_0[16] = { 0, 1, 2, 3, 4, 5, 6, 8, 0, 1, 2, 3, 4, 5, 6, 8 };
			const auto cnt = _bit_count_0[h];
			switch (h) {
				case 6: {
					vector16 tmp;
					src = decode_rle_flat(src, end, tmp.u8, 0);
					if STENOS_UNLIKELY (!src)
						return nullptr;
					fast_copyleft_flat(dst, tmp.u8, x == 0 ? 0 : dst[-1], 0);
				} break;
				case 7:
					src = decode_rle_flat(src, end, dst, x == 0 ? 0 : dst[-1]);
					if STENOS_UNLIKELY (!src)
						return nullptr;
					break;
				case 15:
					// check overflow
					if STENOS_UNLIKELY (end - src < 16)
						return nullptr;
					memcpy(dst, src, 16);
					src += 16;
					break;
				case 0: {
					// check overflow
					if STENOS_UNLIKELY (end < src)
						return nullptr;
					auto min = mins[x];
					memset(dst, (int)min, 16);
				} break;
				case 1:
				case 2:
				case 3:
				case 4:
				case 5: {
					// check overflow
					if STENOS_UNLIKELY (end < src + cnt * 2)
						return nullptr;
					auto min = mins[x];
					// read compressed column directly into dst (in/out)
					src = read_16_bits(src, end, dst, cnt);
					fast_offset_flat(dst, min);
				} break;
				default: {
					// check overflow
					if STENOS_UNLIKELY (end < src + cnt * 2)
						return nullptr;
					auto min = mins[x];
					if (cnt > 0) {
						// read compressed column directly into dst (in/out)
						src = read_16_bits(src, end, dst, cnt);
					}
					fast_copyleft_flat(dst, cnt > 0 ? dst : nullptr, x == 0 ? 0 : dst[-1], min);
				} break;
			}

			return src;
		}

		static STENOS_ALWAYS_INLINE const uint8_t* decode_block_flat_(const uint8_t* STENOS_RESTRICT src,
									      uint8_t* STENOS_RESTRICT dst,
									      const uint8_t* STENOS_RESTRICT end,
									      const uint8_t* STENOS_RESTRICT headers,
									      const uint8_t* STENOS_RESTRICT mins) noexcept
		{
			// decode rows
			for (uint32_t i = 0; i < 16; i += 2, dst += 32) {
				// check overflow
				if STENOS_UNLIKELY (!src)
					return nullptr;
				src = decolde_line_flat(headers[i], src, end, dst, i, mins);
				if STENOS_UNLIKELY (!src)
					return nullptr;
				src = decolde_line_flat(headers[i + 1], src, end, dst + 16, i + 1, mins);
			}
			return src;
		}

		static inline const uint8_t* decode_block_flat(const uint8_t* STENOS_RESTRICT src, uint8_t* STENOS_RESTRICT dst, const uint8_t* STENOS_RESTRICT end) noexcept
		{
			if STENOS_UNLIKELY (src + 8 + 16 > end)
				return nullptr;
			vector16 headers, mins;
			auto min_start = src + 8;
			for (unsigned i = 0; i < 16; i += 2, ++src) {
				auto h0 = headers.u8[i] = *src & 0xF;
				auto h1 = headers.u8[i + 1] = *src >> 4;
				if (h0 != 6 && h0 != 7 && h0 != 15)
					mins.u8[i] = *min_start++;
				if (h1 != 6 && h1 != 7 && h1 != 15)
					mins.u8[i + 1] = *min_start++;
			}
			return decode_block_flat_(min_start, dst, end, headers.u8, mins.u8);
		}

		static inline const uint8_t* decode_block_flat_rle(const uint8_t* STENOS_RESTRICT src, uint8_t* STENOS_RESTRICT dst, const uint8_t* STENOS_RESTRICT end) noexcept
		{
			if STENOS_UNLIKELY (src + 8 > end)
				return nullptr;

			// read headers and compressed mins
			vector16 mins;
			vector16 headers;
			src = read_16_bits(src, end, headers.u8, 4);
			src = decode_rle_flat(src, end, mins.u8, 0);
			if STENOS_UNLIKELY (!src)
				return nullptr;
			return decode_block_flat_(src, dst, end, headers.u8, mins.u8);
		}

	} // end namespace detail

	static STENOS_ALWAYS_INLINE size_t block_decompress_sse(const void* STENOS_RESTRICT _src, size_t size, size_t bytesoftype, size_t bytes, void* STENOS_RESTRICT _dst) noexcept
	{
		if STENOS_UNLIKELY (bytes == 0 || size == 0)
			return 0;

		const uint8_t* src = static_cast<const uint8_t*>(_src);
		const uint8_t* saved = src;
		const uint8_t* end = src + size;
		const uint32_t header_len = (uint32_t)((bytesoftype >> 1) + ((bytesoftype & 1) ? 1 : 0));
		uint8_t* dst = static_cast<uint8_t*>(_dst);

		size_t block_size = bytesoftype * 256;
		size_t block_count = bytes == block_size ? 1 : bytes / block_size;

		// check for minimum size
		if STENOS_UNLIKELY (size < (header_len + bytesoftype) && block_count)
			return STENOS_ERROR_SRC_OVERFLOW;

		// We need a buffer for unshuffle
		uint8_t* buf = (uint8_t*)make_compression_buffer(block_size);
		if STENOS_UNLIKELY (!buf)
			return STENOS_ERROR_ALLOC;

		for (size_t bcount = 0; bcount < block_count; ++bcount, dst += block_size) {

			const uint8_t* anchor = (src);
			src += header_len;
			if STENOS_UNLIKELY (src >= end)
				return STENOS_ERROR_SRC_OVERFLOW;

			if (*anchor == __STENOS_BLOCK_COPY) {
				src = anchor + 1;
				memcpy(dst, src, block_size);
				src += block_size;
				continue;
			}
			if (*anchor == __STENOS_BLOCK_LZ) {
				src = anchor + 1;
				src = lz_decompress_generic(src, dst, bytesoftype, (size_t)(end - src));
				if STENOS_UNLIKELY (!src)
					return STENOS_ERROR_INVALID_INPUT;
				continue;
			}

			for (uint32_t i = 0; i < (uint32_t)bytesoftype; ++i) {

				uint8_t block_header = (anchor[i >> 1] >> (4 * (i & 1))) & 15;
				switch (block_header) {
					case __STENOS_BLOCK_ALL_RAW:
						src = detail::decode_raw_flat(src, buf + i * 256, end);
						break;
					case __STENOS_BLOCK_ALL_SAME:
						src = detail::decode_same_flat(src, buf + i * 256, end);
						break;
					case __STENOS_BLOCK_NORMAL:
						src = detail::decode_block_flat(src, buf + i * 256, end);
						break;
					case __STENOS_BLOCK_NORMAL_RLE:
						src = detail::decode_block_flat_rle(src, buf + i * 256, end);
						break;
					default:
						return STENOS_ERROR_INVALID_INPUT;
				}
				if STENOS_UNLIKELY (!src)
					return STENOS_ERROR_SRC_OVERFLOW;
			}

			unshuffle(bytesoftype, block_size, buf, (uint8_t*)dst);
		}

		size_t remaining_bytes = bytes - (block_size * block_count);
		if (remaining_bytes) {
			if (src == end)
				return STENOS_ERROR_SRC_OVERFLOW;

			uint32_t code = *src++;

			if STENOS_UNLIKELY (code != __STENOS_BLOCK_PARTIAL)
				return STENOS_ERROR_INVALID_INPUT;

			auto r = block_decompress_partial(src, end - src, bytesoftype, bytes - block_size * block_count, (uint8_t*)(_dst) + block_size * block_count);
			if STENOS_UNLIKELY (has_error(r))
				return r;
			src += r;
		}

		return static_cast<size_t>(src - saved);
	}
}

#endif

namespace stenos
{
	static inline size_t block_decompress_generic(const void* STENOS_RESTRICT src, size_t size, size_t bytesoftype, size_t bytes, void* STENOS_RESTRICT dst) noexcept
	{
		STENOS_ASSERT_DEBUG(bytesoftype < STENOS_MAX_BYTESOFTYPE, "invalid bytesoftype");
#ifdef __SSE4_1__
		if (cpu_features().HAS_SSE3)
			return block_decompress_sse(src, size, bytesoftype, bytes, dst);
#endif
		return block_decompress(src, size, bytesoftype, bytes, dst);
	}

	static inline size_t block_compress_generic(const void* STENOS_RESTRICT src,
								  size_t bytesoftype,
								  size_t bytes,
								  void* STENOS_RESTRICT dst,
								  size_t dst_size,
								  int block_level,
								  int full_level,
								  TimeConstraint& t,
								  double* STENOS_RESTRICT target_ratio,
								  const void* STENOS_RESTRICT __shuffled) noexcept
	{
		STENOS_ASSERT_DEBUG(bytesoftype < STENOS_MAX_BYTESOFTYPE, "invalid bytesoftype");
#ifdef __SSE4_1__
		if STENOS_LIKELY (cpu_features().HAS_SSE41) {
			return block_compress(src, bytesoftype, bytes, dst, dst_size, block_level, full_level, t, target_ratio, __shuffled);
		}
#endif
		return STENOS_ERROR_INVALID_INSTRUCTION_SET;
	}
}

#endif
