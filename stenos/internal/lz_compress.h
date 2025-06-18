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

#ifndef STENOS_LZ_COMPRESS_H
#define STENOS_LZ_COMPRESS_H

#include "../bits.hpp"

namespace stenos
{
	static STENOS_ALWAYS_INLINE unsigned read_24(const void* in) noexcept
	{
		// Read 24 bits
		unsigned res = 0;
		memcpy(&res, in, 3);
		return res;
	}
	static STENOS_ALWAYS_INLINE uint64_t read_48(const void* in) noexcept
	{
		// Read 48 bits
		uint64_t res = 0;
		memcpy(&res, in, 6);
		return res;
	}

	static STENOS_ALWAYS_INLINE unsigned hash_val(unsigned v) noexcept
	{
		// Hash 32 bits value
		return (v * 2654435761U) & 255u;
	}
	static STENOS_ALWAYS_INLINE unsigned hash_val64(uint64_t v) noexcept
	{
		// Hash 64 bits value
		return (v * 14313749767032793493ULL) >> 56;
	}

	template<unsigned Bytes>
	void hash_8(const uint8_t* in, unsigned* out) noexcept {};

	template<>
	STENOS_ALWAYS_INLINE void hash_8<8>(const uint8_t* in, unsigned* out) noexcept
	{
		// Hash 8 * 64 bits values
		out[0] = hash_val64(read_64(in));
		out[1] = hash_val64(read_64(in + 8));
		out[2] = hash_val64(read_64(in + 16));
		out[3] = hash_val64(read_64(in + 24));
		out[4] = hash_val64(read_64(in + 32));
		out[5] = hash_val64(read_64(in + 40));
		out[6] = hash_val64(read_64(in + 48));
		out[7] = hash_val64(read_64(in + 56));
	}
	template<>
	STENOS_ALWAYS_INLINE void hash_8<4>(const uint8_t* in, unsigned* out) noexcept
	{
		// Hash 8 * 32 bits values
		out[0] = hash_val(read_32(in));
		out[1] = hash_val(read_32(in + 4));
		out[2] = hash_val(read_32(in + 8));
		out[3] = hash_val(read_32(in + 12));
		out[4] = hash_val(read_32(in + 16));
		out[5] = hash_val(read_32(in + 20));
		out[6] = hash_val(read_32(in + 24));
		out[7] = hash_val(read_32(in + 28));
	}
	template<>
	STENOS_ALWAYS_INLINE void hash_8<3>(const uint8_t* in, unsigned* out) noexcept
	{
		// Hash 8 * 24 bits values
		out[0] = hash_val(read_24(in));
		out[1] = hash_val(read_24(in + 3));
		out[2] = hash_val(read_24(in + 6));
		out[3] = hash_val(read_24(in + 9));
		out[4] = hash_val(read_32(in + 12));
		out[5] = hash_val(read_32(in + 15));
		out[6] = hash_val(read_32(in + 18));
		out[7] = hash_val(read_32(in + 21));
	}
	template<>
	STENOS_ALWAYS_INLINE void hash_8<6>(const uint8_t* in, unsigned* out) noexcept
	{
		// Hash 8 * 48 bits values
		out[0] = hash_val64(read_48(in));
		out[1] = hash_val64(read_48(in + 6));
		out[2] = hash_val64(read_48(in + 12));
		out[3] = hash_val64(read_48(in + 18));
		out[4] = hash_val64(read_48(in + 24));
		out[5] = hash_val64(read_48(in + 30));
		out[6] = hash_val64(read_48(in + 36));
		out[7] = hash_val64(read_48(in + 42));
	}

	template<unsigned Bytes>
	STENOS_ALWAYS_INLINE bool compare_equal(const void* v1, const void* v2) noexcept
	{
		return false;
	}
	template<>
	STENOS_ALWAYS_INLINE bool compare_equal<3>(const void* v1, const void* v2) noexcept
	{
		return read_24(v1) == read_24(v2);
	}
	template<>
	STENOS_ALWAYS_INLINE bool compare_equal<4>(const void* v1, const void* v2) noexcept
	{
		return read_32(v1) == read_32(v2);
	}
	template<>
	STENOS_ALWAYS_INLINE bool compare_equal<6>(const void* v1, const void* v2) noexcept
	{
		return read_48(v1) == read_48(v2);
	}
	template<>
	STENOS_ALWAYS_INLINE bool compare_equal<8>(const void* v1, const void* v2) noexcept
	{
		return read_64(v1) == read_64(v2);
	}

	static STENOS_ALWAYS_INLINE uint8_t* write_diff(uint16_t diff, uint8_t* out) noexcept
	{
		// Write 15 bits unsigned in a compact way
		if (diff < 128)
			*out++ = static_cast<uint8_t>(diff);
		else {
			out[0] = (diff & 127) | (1 << 7);
			out[1] = (diff >> 7);
			out += 2;
		}
		return out;
	}

	template<unsigned Bytes>
	static STENOS_ALWAYS_INLINE uint8_t* inline_memcpy(uint8_t* dst, const uint8_t* src) noexcept
	{
		// Memcpy number of bytes known at compile time
		memcpy(dst, src, Bytes);
		return dst + Bytes;
	}

	template<unsigned Bytes>
	static STENOS_ALWAYS_INLINE uint8_t* process2(size_t pos, const uint8_t* start, const uint8_t* in, uint8_t* out, unsigned* h, uint8_t* anchor, uint8_t shift, uint16_t* buffer) noexcept
	{
		// Process 2 input values of size Bytes

		// Check for match
		uint8_t s0 = (buffer[h[0]] < pos && compare_equal<Bytes>(start + buffer[h[0]] * Bytes, in));

		// Write match or raw value
		if (s0)
			out = write_diff((uint16_t)(pos - buffer[h[0]]), out);
		else
			out = inline_memcpy<Bytes>(out, in);

		buffer[h[0]] = (uint16_t)pos;
		uint8_t s1 = (buffer[h[1]] < pos + 1 && compare_equal<Bytes>(start + buffer[h[1]] * Bytes, in + Bytes));

		if (s1)
			out = write_diff((uint16_t)(pos + 1 - buffer[h[1]]), out);
		else
			out = inline_memcpy<Bytes>(out, in + Bytes);

		buffer[h[1]] = (uint16_t)(pos + 1);

		// Update anchor
		*anchor |= (s0 | (s1 << 1)) << shift;

		return out;
	}

	template<unsigned Bytes>
	static inline uint8_t* lz_compress(const uint8_t* _in, uint8_t* dst, size_t count, size_t max_size, uint16_t* buffer) noexcept
	{
		const uint8_t* in = _in;
		uint8_t* out = dst;
		unsigned h[8];
		unsigned failed = 0;
		unsigned max_failed = 3;
		uint8_t once = 0;

		for (size_t i = 0; i < count; i += 8, in += Bytes * 8) {

			uint8_t* anchor = out++;
			*anchor = 0;

			if (failed == max_failed) {
				failed = 0;
				if (--max_failed == 0)
					max_failed = 1;
				out = inline_memcpy<Bytes * 8>(out, in);
			}
			else {
				hash_8<Bytes>(in, h);
				out = process2<Bytes>(i, _in, in, out, h, anchor, 0, buffer);
				out = process2<Bytes>(i + 2, _in, in + Bytes * 2, out, h + 2, anchor, 2, buffer);
				out = process2<Bytes>(i + 4, _in, in + Bytes * 4, out, h + 4, anchor, 4, buffer);
				out = process2<Bytes>(i + 6, _in, in + Bytes * 6, out, h + 6, anchor, 6, buffer);
				failed += *anchor == 0;
			}

			size_t produced = out - dst;
			if (produced > max_size)
				return nullptr;
			if (!once && i > count / 4) {
				// check for early stop
				if ((double)produced > (double)max_size * 0.4)
					return nullptr;
				once = 1;
			}
		}
		return out;
	}

	template<unsigned Bytes>
	static inline const uint8_t* lz_decompress(const uint8_t* in, uint8_t* _dst, size_t count, size_t in_size) noexcept
	{
		uint8_t* dst = _dst;
		const uint8_t* end = in + in_size;
		for (size_t i = 0; i < count; i += 8) {

			// We need at least 2 bytes for decoding
			if STENOS_UNLIKELY (in + 2 > end)
				return nullptr;

			uint8_t anchor = *in++;
			if (anchor == 0) {
				// 8 raw values
				if STENOS_UNLIKELY (in + 8 * Bytes > end)
					return nullptr;
				dst = inline_memcpy<8 * Bytes>(dst, in);
				in += 8 * Bytes;
				continue;
			}

			for (uint8_t j = 0; j < 8; ++j) {
				if ((anchor >> j) & 1) {
					// Copy Bytes from offset in dst
					uint16_t offset = *in & 127U;
					if (*in++ > 127U) {
						if STENOS_UNLIKELY (in == end)
							return nullptr;
						offset |= ((*in++) << 7U);
					}
					STENOS_ASSERT_DEBUG(dst - offset * Bytes >= _dst, "");
					dst = inline_memcpy<Bytes>(dst, dst - offset * Bytes);
				}
				else {
					// Copy Bytes from input
					if STENOS_UNLIKELY (in + Bytes > end)
						return nullptr;
					dst = inline_memcpy<Bytes>(dst, in);
					in += Bytes;
				}
			}
		}
		return in;
	}

	static STENOS_ALWAYS_INLINE uint8_t* lz_compress_generic(const uint8_t* _in, uint8_t* dst, size_t bytesoftype, size_t max_size, uint16_t* buffer) noexcept
	{
		if (bytesoftype > 512)
			// Cannot process bigger bytesoftype because we use 15 bits indices on the hash table
			return nullptr;

		if (bytesoftype % 8 == 0) {
			return lz_compress<8>(_in, dst, (256 * bytesoftype) / 8, max_size, buffer);
		}
		else if (bytesoftype <= 2 || bytesoftype % 4 == 0) {
			return lz_compress<4>(_in, dst, (256 * bytesoftype) / 4, max_size, buffer);
		}
		else if (bytesoftype % 6 == 0) {
			return lz_compress<6>(_in, dst, (256 * bytesoftype) / 6, max_size, buffer);
		}
		else if (bytesoftype % 3 == 0) {
			return lz_compress<3>(_in, dst, (256 * bytesoftype) / 3, max_size, buffer);
		}

		return nullptr;
	}

	static STENOS_ALWAYS_INLINE const uint8_t* lz_decompress_generic(const uint8_t* in, uint8_t* dst, size_t bytesoftype, size_t in_size) noexcept
	{
		if (bytesoftype > 512)
			// Cannot process bigger bytesoftype because we use 15 bits indices on the hash table
			return nullptr;

		if (bytesoftype % 8 == 0) {
			return lz_decompress<8>(in, dst, (256 * bytesoftype) / 8, in_size);
		}
		else if (bytesoftype <= 2 || bytesoftype % 4 == 0) {
			return lz_decompress<4>(in, dst, (256 * bytesoftype) / 4, in_size);
		}
		else if (bytesoftype % 6 == 0) {
			return lz_decompress<6>(in, dst, (256 * bytesoftype) / 6, in_size);
		}
		else if (bytesoftype % 3 == 0) {
			return lz_decompress<3>(in, dst, (256 * bytesoftype) / 3, in_size);
		}

		return nullptr;
	}

}
#endif
