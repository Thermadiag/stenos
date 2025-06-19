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

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif

#include "block_compress.h"
#include "delta.h"

#define STENOS_FRAME_HEADER_BLOCK (1)		      // Bytes compressed with block encoder only
#define STENOS_FRAME_HEADER_ZSTD (2)		      // Bytes compressed with zstd only
#define STENOS_FRAME_HEADER_TRANSPOSED_ZSTD (3)	      // Bytes compressed with zstd on transposed input
#define STENOS_FRAME_HEADER_TRANSPOSED_DELTA_ZSTD (4) // Bytes compressed with zstd on transposed + delta input
#define STENOS_FRAME_HEADER_BLOCK_ZSTD (5)	      // Bytes compressed with blocks + zstd
#define STENOS_FRAME_HEADER_COPY (6)		      // Bytes memcopied

namespace stenos
{
	/// @brief Compression/decompression buffer class
	/// used by steno_compress/decompress functions
	struct CBuffer
	{
		// Additional infos
		void* dst{ nullptr };
		size_t dst_size{ 0 };

		// Compression buffer
		char* bytes{ nullptr };

		static CBuffer* make(size_t bytes) noexcept
		{
			size_t alloc = bytes + 16 + sizeof(CBuffer); // Add 16 to ensure aligned access
			CBuffer* res = (CBuffer*)malloc(alloc);
			if (!res)
				return res;

			new (res) CBuffer();
			res->bytes = (char*)detail::align_buffer((char*)res + sizeof(CBuffer));
			STENOS_ASSERT_DEBUG((uintptr_t)res->bytes % 16 == 0, "Unaligned bytes!");
			return res;
		}

		static void destroy(CBuffer* buf) noexcept { free(buf); }
	};

	/// @brief Helper function, returns the superblock size for given block size (bytesoftype * 256)
	static STENOS_ALWAYS_INLINE size_t super_block_size(size_t block_size) noexcept
	{
		if (block_size > STENOS_BLOCK_SIZE)
			return block_size;
		return (STENOS_BLOCK_SIZE / block_size) * block_size;
	}

}

// Compression/decompression context
struct stenos_context_s
{
	// Buffers
	std::vector<stenos::CBuffer*> thread_buffers;
	std::vector<stenos::CBuffer*> tmp_buffers1;
	std::vector<stenos::CBuffer*> tmp_buffers2;

	// Superblock size
	size_t superblock_size{ 0 };

	// Time constraint
	stenos::TimeConstraint t;

	// Parameters
	int threads{ 1 };
	int level{ 1 };
	int shift{ 0 };
	size_t custom_blocksize_shift{ STENOS_NO_BLOCK_SHIFT };

	STENOS_ALWAYS_INLINE void reset_parameters() noexcept
	{
		t.nanoseconds = 0;
		threads = 1;
		level = 1;
		custom_blocksize_shift = STENOS_NO_BLOCK_SHIFT;
	}

	STENOS_ALWAYS_INLINE double requested_speed() noexcept
	{
		// Compute the requested speed in B/s for the remaining bytes
		auto remaining = (t.nanoseconds - t.timer.tock()) * 1e-9;
		return (t.total_bytes - t.processed_bytes.load(std::memory_order_relaxed)) / remaining;
	}

	size_t prepare(size_t bytesoftype, size_t bytes) noexcept
	{
		// Prepare the compresson of given number of bytes

		if STENOS_UNLIKELY (bytesoftype == 0 || bytesoftype >= STENOS_MAX_BYTESOFTYPE)
			return STENOS_ERROR_INVALID_BYTESOFTYPE;

		size_t block_size = bytesoftype * 256;
		size_t new_superblock_size = 0;
		shift = 0;

		if (t.nanoseconds) {
			// try to have at least thread * 32 super block
			size_t required_superblock_count = threads * 32;
			size_t required_superblock_size = bytes / required_superblock_count;

			size_t block_count = required_superblock_size / block_size;
			if (block_count == 0)
				block_count = 1;

			new_superblock_size = block_size * block_count;
			shift = 255;
			if (new_superblock_size >= STENOS_MAX_BLOCK_BYTES) {
				// Default strategy for level 9
				new_superblock_size = stenos::super_block_size(block_size);
				if (bytes > new_superblock_size) {
					shift = (9 - 1) / 2;
					new_superblock_size = new_superblock_size << (size_t)shift;
				}
			}
			else if (new_superblock_size < STENOS_BLOCK_SIZE) {
				// Default strategy for level 1
				new_superblock_size = stenos::super_block_size(block_size);
			}
		}
		else {

			if (custom_blocksize_shift != STENOS_NO_BLOCK_SHIFT) {
				// Custom block shift
				new_superblock_size = block_size << custom_blocksize_shift;
				shift = 255;
			}
			else {
				// Compute superblock size
				new_superblock_size = stenos::super_block_size(block_size);
				if (bytes > new_superblock_size) {
					shift = level ? (level - 1) / 2 : 0;
					new_superblock_size = new_superblock_size << (size_t)shift;
				}
			}
		}

		// Check superblock size validity
		if STENOS_UNLIKELY (new_superblock_size < block_size || new_superblock_size >= STENOS_MAX_BLOCK_BYTES)
			return STENOS_ERROR_INVALID_PARAMETER;

		// Clear buffers if necessary
		if (new_superblock_size != superblock_size) {
			superblock_size = new_superblock_size;
			clear_buffers();
		}

		// Initialize the time constraint
		if (t.nanoseconds) {
			t.total_bytes = bytes;
			t.finish_memcpy.store(false);
			t.processed_bytes.store(0);
			t.timer.tick();
		}
		return 0;
	}

	STENOS_ALWAYS_INLINE size_t ensure_has_buffers(int size) noexcept
	{
		// Ensure that we have the requested number of buffers
		try {
			if (thread_buffers.size() < (size_t)size) {
				thread_buffers.resize((size_t)size, nullptr);
				tmp_buffers1.resize((size_t)size, nullptr);
				tmp_buffers2.resize((size_t)size, nullptr);
			}
		}
		catch (...) {
			return STENOS_ERROR_ALLOC;
		}
		return 0;
	}

	STENOS_ALWAYS_INLINE void clear_buffers() noexcept
	{
		// Clear and reset all buffers

		for (size_t i = 0; i < thread_buffers.size(); ++i) {
			if (thread_buffers[i]) {
				stenos::CBuffer::destroy(thread_buffers[i]);
				thread_buffers[i] = nullptr;
			}
		}
		thread_buffers.clear();
		for (size_t i = 0; i < tmp_buffers1.size(); ++i) {
			if (tmp_buffers1[i]) {
				stenos::CBuffer::destroy(tmp_buffers1[i]);
				tmp_buffers1[i] = nullptr;
			}
			if (tmp_buffers2[i]) {
				stenos::CBuffer::destroy(tmp_buffers2[i]);
				tmp_buffers2[i] = nullptr;
			}
		}
		tmp_buffers1.clear();
		tmp_buffers2.clear();
	}

	STENOS_ALWAYS_INLINE ~stenos_context_s() noexcept { clear_buffers(); }
};

stenos_context* stenos_make_context()
{
	stenos_context* ctx = (stenos_context*)malloc(sizeof(stenos_context));
	return new (ctx) stenos_context_s();
}

void stenos_destroy_context(stenos_context* ctx)
{
	if (ctx) {
		ctx->~stenos_context();
		free(ctx);
	}
}

void stenos_reset_context(stenos_context* ctx)
{
	if (ctx) {
		ctx->level = 1;
		ctx->threads = 1;
		ctx->t.nanoseconds = 0;
	}
}

size_t stenos_set_level(stenos_context* ctx, int level)
{
	if (level > 9)
		level = 9;
	else if (level < 0)
		level = 0;
	ctx->level = level;
	return 0;
}

size_t stenos_set_threads(stenos_context* ctx, int threads)
{
	ctx->threads = threads < 1 ? 1 : threads;
	return 0;
}

size_t stenos_set_max_nanoseconds(stenos_context* ctx, uint64_t nanoseconds)
{
	ctx->t.nanoseconds = nanoseconds;
	return 0;
}

size_t stenos_set_block_size(stenos_context* ctx, size_t blocksize_shift)
{
	if (blocksize_shift >= 16 && blocksize_shift != STENOS_NO_BLOCK_SHIFT)
		// Block shift of 16 or more is impossible.
		// For the smallest BPP (1), that would mean
		// a superblock size of STENOS_MAX_BLOCK_BYTES
		// (which is forbidden)
		return STENOS_ERROR_INVALID_PARAMETER;
	ctx->custom_blocksize_shift = blocksize_shift;
	return 0;
}

int stenos_has_error(size_t r)
{
	// Check for error code
	return stenos::has_error(r);
}

size_t stenos_bound(size_t bytes)
{
	// Maximum compressed size for given input bytes
	return stenos::compress_bound(bytes);
}

namespace stenos
{
	static STENOS_ALWAYS_INLINE void write_uint32_3(void* dst, unsigned val) noexcept
	{
		// Write 32 bits integer on 3 bytes
		char tmp[4];
		write_LE_32(tmp, val);
		memcpy(dst, tmp, 3);
	}

	static STENOS_ALWAYS_INLINE unsigned read_uint32_3(const void* src) noexcept
	{
		// Read 32 bits integer from 3 bytes
		unsigned res = 0;
		memcpy(&res, src, 3);
#if STENOS_BYTEORDER_ENDIAN == STENOS_BYTEORDER_BIG_ENDIAN
		res = byte_swap_32(res);
#endif
		return res;
	}

	static STENOS_ALWAYS_INLINE void write_uint64_7(void* dst, uint64_t val) noexcept
	{
		// Write 64 bits integer on 7 bytes
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		val = byte_swap_64(val);
#endif
		memcpy(dst, &val, 7);
	}

	static STENOS_ALWAYS_INLINE uint64_t read_uint64_7(const void* src) noexcept
	{
		// Read 64 bits integer from 7 bytes
		uint64_t res = 0;
		memcpy(&res, src, 7);
#if STENOS_BYTEORDER_ENDIAN == STENOS_BYTEORDER_BIG_ENDIAN
		res = byte_swap_64(res);
#endif
		return res;
	}

	static inline size_t compress_memcpy(const void* src, size_t bytes, void* _dst, size_t dst_size) noexcept
	{
		// "Compress" using plain memcpy
		if (dst_size < bytes + 4)
			return STENOS_ERROR_DST_OVERFLOW;
		uint8_t* dst = (uint8_t*)_dst;
		*dst++ = STENOS_FRAME_HEADER_COPY;
		write_uint32_3(dst, (unsigned)bytes);
		dst += 3;
		memcpy(dst, src, bytes);
		return bytes + 4;
	}

	static inline double guess_transposed_lz_ratio(const void* src, size_t bytesoftype, size_t bytes, int level, CBuffer* delta_buffer)
	{
		// Try to guess lz compression ratio on input
		size_t elements = bytes / bytesoftype;
		size_t stepsize = elements / (16 / (level - 1)); // Check more bytes for higher levels
		size_t csize = 0;
		size_t processed = 0;

		for (size_t i = 0; i < bytesoftype; ++i) {

			auto input1 = (const char*)src + i * elements + (elements - stepsize) / 2;
			if (delta_buffer) {
				// apply delta
				auto dst1 = (char*)delta_buffer->bytes + i * (elements - stepsize) / 2;
				delta(input1, dst1, stepsize);
				input1 = dst1;
			}
			// Adjust acceleration based on level
			csize += stenos::lz4_guess_size(input1, stepsize, 10 - level);
			processed += stepsize;
		}
		// Adjust result base on level (higher level should perform better)
		return ((double)(processed) / (double)csize) * (1. + (double)level * 0.02);
	}

	static STENOS_ALWAYS_INLINE size_t
	compress_generic_superblock(stenos_context_s* ctx, const void* src, size_t bytesoftype, size_t bytes, void* _dst, size_t dst_size, CBuffer*& buffer1, CBuffer*& buffer2) noexcept
	{
		// Compress a superblock and returns the compressed size.
		// Check different strategies and pick the best one among:
		//	- SIMD based block compression by chunk of 256 elements. Combination of delta coding, bit packing, RLE, basic LZ (+ ZSTD over block compression)
		//	- Direct ZSTD compression
		//	- ZSTD compression on transposed input (similar to blosc)
		//	- ZSTD compression on transposed input + byte delta.
		//
		// The output size is at most bytes + 4.

		STENOS_ASSERT_DEBUG(bytes % bytesoftype == 0, "invalid input byte size");

		size_t result = 0;
		uint8_t* dst = (uint8_t*)_dst;
		uint8_t* dst_end = dst + dst_size;
		const bool time_limited = ctx->t.nanoseconds != 0;
		int block_level = 2, zstd_level = 0;
		int level = time_limited ? 9 : ctx->level;

		if STENOS_UNLIKELY (dst_size < 4)
			// We need at least 4 bytes to write the superblock header
			return STENOS_ERROR_DST_OVERFLOW;

		if STENOS_UNLIKELY (bytes == 0 || ctx->t.finish_memcpy.load(std::memory_order_relaxed) || (ctx->level == 0 && !time_limited))
			// Empty input or too slow compression: direct copy
			goto MEMCPY;

		if (bytes < 128)
			// Small input: direct zstd
			goto ZSTD;

		if (!time_limited) {
			// Adjust zstd level (1 to 9)
			if (bytesoftype > 1) {
				// level 1 becomes block level 2
				if (level < 2)
					goto BLOCK;
				else {
					// skip level 4 that does not bring much
					zstd_level = level - 1;
					if (zstd_level >= 4)
						++zstd_level;
				}
			}
			else
				zstd_level = level;
		}

		{
			// Try combination of block and/or zstd compression
			int glevel = level;		      // level for LZ compression guessing
			double target_speed = 0;	      // requested speed in bytes/second
			double lz_transposed_ratio = 0;	      // lz ratio on transposed input
			double lz_transposed_delta_ratio = 0; // lz ratio on transposed input + delta
			double lz_ratio = 1.1;		      // lz ratio on raw input. 1.1 is high enough to discard block compression on "uncompressible" content (like text)

			if (time_limited) {

				// Compute requested speed in bytes/second.
				// Uses remaining bytes and time.
				target_speed = ctx->requested_speed();

				// Adjust level used to guess lz compression ratio.
				if (target_speed < 10000000)
					glevel = 9;
				else if (target_speed < 40000000)
					glevel = 8;
				else if (target_speed < 100000000)
					glevel = 7;
				else if (target_speed < 200000000)
					glevel = 6;
				else if (target_speed < 400000000)
					glevel = 5;
				else
					glevel = 2;
			}

			if (target_speed < 600000000 && bytes >= bytesoftype * 256) {
				// If high speed required (above 600MB/s), don't check for lz ratio
				lz_ratio = lz4_guess_ratio((const char*)src, bytes / 16, 10 - glevel);
			}

			if (target_speed > 1500000000 && bytesoftype > 1) {
				// Very high speed requested: direct block compression
				goto BLOCK;
			}

			// Create the buffers
			if (!buffer1)
				buffer1 = CBuffer::make(ctx->superblock_size + 4); // Add 4 for the superblock header
			if (!buffer2)
				buffer2 = CBuffer::make(ctx->superblock_size + 4); // Add 4 for the superblock header
			if STENOS_UNLIKELY (!buffer1 || !buffer2)
				goto ZSTD;

			if (bytesoftype > 1) {

				// Transpose in buffer
				shuffle(bytesoftype, bytes, (uint8_t*)src, (uint8_t*)buffer1->bytes);

				if (target_speed < 600000000 && bytes >= bytesoftype * 256 && level > 2) {

					// If high speed required (or level <= 2), don't check for lz ratio on transposed input

					lz_transposed_ratio = guess_transposed_lz_ratio(buffer1->bytes, bytesoftype, bytes, glevel, 0);
					if (lz_transposed_ratio > lz_ratio)
						lz_ratio = lz_transposed_ratio;

					lz_transposed_delta_ratio = guess_transposed_lz_ratio(buffer1->bytes, bytesoftype, bytes, glevel, buffer2) * 1.1;
					if (lz_transposed_delta_ratio > lz_ratio)
						lz_ratio = lz_transposed_delta_ratio;

					if (target_speed < 2000000 && level == 9) {
						// For highest level, try to favor ZSTD compression
						// as it usually always outperform block compression
						// for its highest levels
						const double factor = 1.6;
						lz_transposed_ratio *= factor;
						lz_transposed_delta_ratio *= factor;
						lz_ratio *= factor;
					}
				}
			}

			// Try block compression
			uint64_t tick = time_limited ? ctx->t.timer.tock() : 0;
			size_t cblock =
			  stenos::block_compress_generic(src, bytesoftype, bytes, buffer2->bytes, bytes, block_level, level, ctx->t, &lz_ratio, bytesoftype > 1 ? buffer1->bytes : nullptr);
			if (has_error(cblock) || cblock > bytes) {
				// Failed: compression ratio too low
				if (lz_ratio > 1.4) {
					// Under 1.4 ratio, direct entropy compression should be used (direct zstd)
					if (lz_ratio == lz_transposed_ratio)
						goto TRANSPOSED_ZSTD;
					if (lz_ratio == lz_transposed_delta_ratio)
						goto TRANSPOSED_DELTA_ZSTD;
				}
				goto ZSTD;
			}

			if (time_limited) {

				// Compute required compression level for zstd

				auto el = ctx->t.timer.tock();
				auto block_el = el - tick;

				size_t processed = ctx->t.processed_bytes.load(std::memory_order_relaxed) + cblock;
				double global_block_speed = processed / (el * 1e-9);
				double current_block_speed = bytes / (block_el * 1e-9);

				zstd_level = 0;
				if (global_block_speed > target_speed && current_block_speed > target_speed) {
					size_t zstd_rate = (size_t)((current_block_speed * target_speed) / (current_block_speed - target_speed));
					zstd_level = detail::clevel_for_remaining(ctx->t, processed, &zstd_rate, 1);
				}
				
				if (zstd_level < 1)
					// No more time to perform zstd compression, copy back to destination
					goto NO_ZSTD;
			}

			// Try zstd on compressed blocks
			result = zstd_compress_with_context(dst + 4, dst_size - 4, buffer2->bytes, cblock, zstd_level);

			if (has_error(result) || result > cblock) {
			NO_ZSTD:
				// zstd failed
				if STENOS_UNLIKELY (dst + 4 + cblock > dst_end)
					return STENOS_ERROR_DST_OVERFLOW;

				*dst++ = STENOS_FRAME_HEADER_BLOCK;
				write_uint32_3(dst, (unsigned)cblock);
				dst += 3;
				memcpy(dst, buffer2->bytes, cblock);
				return cblock + 4;
			}

			// Block compression + zstd
			if STENOS_UNLIKELY (dst + 4 + result > dst_end)
				return STENOS_ERROR_DST_OVERFLOW;
			*dst++ = STENOS_FRAME_HEADER_BLOCK_ZSTD;
			write_uint32_3(dst, (unsigned)result);
			return result + 4;
		}

	BLOCK:
		// Direct block compression
		result = block_compress_generic(src, bytesoftype, bytes, dst + 4, dst_size - 4, block_level, level, ctx->t, nullptr, nullptr);
		if (has_error(result) || result > bytes)
			goto MEMCPY;
		if STENOS_UNLIKELY (dst + 4 + result > dst_end)
			return STENOS_ERROR_DST_OVERFLOW;
		*dst++ = STENOS_FRAME_HEADER_BLOCK;
		write_uint32_3(dst, (unsigned)result);
		return result + 4;

	TRANSPOSED_ZSTD:
		// zstd over transposed input
		if (ctx->t.nanoseconds) {
			size_t processed = ctx->t.processed_bytes.load(std::memory_order_relaxed);
			zstd_level = detail::clevel_for_remaining(ctx->t, processed);
			if (zstd_level <= 0)
				goto MEMCPY;
		}

		// Compress
		result = zstd_compress_with_context(dst + 4, dst_size - 4, buffer1->bytes, bytes, zstd_level);
		if STENOS_UNLIKELY (has_error(result) || result > bytes)
			goto MEMCPY;
		if STENOS_UNLIKELY (dst + 4 + result > dst_end)
			return STENOS_ERROR_DST_OVERFLOW;
		*dst++ = STENOS_FRAME_HEADER_TRANSPOSED_ZSTD;
		write_uint32_3(dst, (unsigned)result);
		return result + 4;

	TRANSPOSED_DELTA_ZSTD:
		// zstd over transposed input + byte delta
		if (ctx->t.nanoseconds) {
			size_t processed = ctx->t.processed_bytes.load(std::memory_order_relaxed);
			zstd_level = detail::clevel_for_remaining(ctx->t, processed);
			if (zstd_level <= 0)
				goto MEMCPY;
		}

		// Fast byte delta
		delta(buffer1->bytes, buffer2->bytes, bytes);

		// Compress
		result = zstd_compress_with_context(dst + 4, dst_size - 4, buffer2->bytes, bytes, zstd_level);
		if STENOS_UNLIKELY (has_error(result) || result > bytes)
			goto MEMCPY;
		if STENOS_UNLIKELY (dst + 4 + result > dst_end)
			return STENOS_ERROR_DST_OVERFLOW;
		*dst++ = STENOS_FRAME_HEADER_TRANSPOSED_DELTA_ZSTD;
		write_uint32_3(dst, (unsigned)result);
		return result + 4;

	ZSTD:
		// Direct zstd compression
		if (ctx->t.nanoseconds) {
			size_t processed = ctx->t.processed_bytes.load(std::memory_order_relaxed);
			zstd_level = detail::clevel_for_remaining(ctx->t, processed);
			if (zstd_level <= 0)
				goto MEMCPY;
		}
		result = zstd_compress_with_context(dst + 4, dst_size - 4, src, bytes, zstd_level);

		if STENOS_UNLIKELY (has_error(result) || result > bytes)
			goto MEMCPY;
		if STENOS_UNLIKELY (dst + 4 + result > dst_end)
			return STENOS_ERROR_DST_OVERFLOW;
		*dst++ = STENOS_FRAME_HEADER_ZSTD;
		write_uint32_3(dst, (unsigned)result);
		return result + 4;

	MEMCPY:
		// Use memcpy
		return compress_memcpy(src, bytes, _dst, dst_size);
	}

	static STENOS_ALWAYS_INLINE size_t
	decompress_generic_superblock(stenos_context_s* ctx, uint8_t code, const uint8_t* src, size_t bytesoftype, size_t csize, uint8_t* dst, size_t dsize, CBuffer*& buffer) noexcept
	{
		// Decompress a superblock

		switch (code) {

			case STENOS_FRAME_HEADER_BLOCK: {
				// Direct block compression
				auto r = block_decompress_generic(src, csize, bytesoftype, dsize, dst);
				if STENOS_UNLIKELY (has_error(r))
					return STENOS_ERROR_INVALID_INPUT;
			} break;
			case STENOS_FRAME_HEADER_ZSTD: {
				// Direct zstd compression
				auto r = ZSTD_decompress(dst, dsize, src, csize);
				if STENOS_UNLIKELY (ZSTD_isError(r))
					return STENOS_ERROR_INVALID_INPUT;
			} break;
			case STENOS_FRAME_HEADER_TRANSPOSED_ZSTD: {
				// zstd on transposed input
				if (!buffer)
					buffer = CBuffer::make(ctx->superblock_size + 4); // Add 4 for the superblock header
				if STENOS_UNLIKELY (!buffer)
					return STENOS_ERROR_ALLOC;
				auto r = ZSTD_decompress(buffer->bytes, dsize, src, csize);
				if STENOS_UNLIKELY (ZSTD_isError(r) || r != dsize)
					return STENOS_ERROR_INVALID_INPUT;
				unshuffle(bytesoftype, dsize, (uint8_t*)buffer->bytes, dst);
			} break;
			case STENOS_FRAME_HEADER_TRANSPOSED_DELTA_ZSTD: {
				// zstd on transposed input + byte delta
				if (!buffer)
					buffer = CBuffer::make(ctx->superblock_size + 4); // Add 4 for the superblock header
				if STENOS_UNLIKELY (!buffer)
					return STENOS_ERROR_ALLOC;
				// Decomrpess to dst
				auto r = ZSTD_decompress(dst, dsize, src, csize);
				if STENOS_UNLIKELY (ZSTD_isError(r) || r != dsize)
					return STENOS_ERROR_INVALID_INPUT;
				// Byte delta inverse to buffer
				delta_inv(dst, buffer->bytes, dsize);
				// Unshuffle to dst
				unshuffle(bytesoftype, dsize, (uint8_t*)buffer->bytes, dst);
			} break;
			case STENOS_FRAME_HEADER_BLOCK_ZSTD: {
				if (!buffer)
					buffer = CBuffer::make(ctx->superblock_size + 4); // Add 4 for the superblock header
				if STENOS_UNLIKELY (!buffer)
					return STENOS_ERROR_ALLOC;
				// zstd decompression
				auto r = ZSTD_decompress(buffer->bytes, ctx->superblock_size, src, csize);
				if STENOS_UNLIKELY (ZSTD_isError(r))
					return STENOS_ERROR_INVALID_INPUT;
				// block decompression
				r = block_decompress_generic(buffer->bytes, r, bytesoftype, dsize, dst);
				if STENOS_UNLIKELY (has_error(r))
					return STENOS_ERROR_INVALID_INPUT;

			} break;
			case STENOS_FRAME_HEADER_COPY:
				// Direct copy
				if STENOS_UNLIKELY (dsize != csize)
					return STENOS_ERROR_INVALID_INPUT;
				memcpy(dst, src, csize);
				break;

			default:
				// Unknown code
				return STENOS_ERROR_INVALID_INPUT;
		}
		return dsize;
	}

	static inline stenos::tiny_pool& get_pool()
	{
		// Create static thread pool
		static stenos::tiny_pool pool(std::thread::hardware_concurrency() * 2u);
		return pool;
	}

	// Global thread pool used for multithreaded compression/decompression.
	// Make sure that threads are created at program initialization.
	static stenos::tiny_pool* pool = &get_pool();

}


size_t stenos_private_compress_block(stenos_context* ctx, const void* src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* dst, size_t dst_size)
{
	// Private API used by cvector, compress a superblock
	if (ctx->superblock_size != super_block_size) {
		ctx->superblock_size = super_block_size;
		ctx->clear_buffers();
	}
	if STENOS_UNLIKELY (stenos::has_error(ctx->ensure_has_buffers(1)))
		return STENOS_ERROR_ALLOC;
	return stenos::compress_generic_superblock(ctx, src, bytesoftype, bytes, dst, dst_size, ctx->tmp_buffers1[0], ctx->tmp_buffers2[0]);
}

size_t stenos_private_decompress_block(stenos_context* ctx, const void* _src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* _dst, size_t dst_size)
{
	// Private API used by cvector, decompress a superblock
	if (ctx->superblock_size != super_block_size) {
		ctx->superblock_size = super_block_size;
		ctx->clear_buffers();
	}
	const uint8_t* src = (const uint8_t*)_src;
	const uint8_t* end_src = src + bytes;
	uint8_t* dst = (uint8_t*)_dst;
	uint8_t* end_dst = dst + dst_size;

	if (src + 4 > end_src)
		return STENOS_ERROR_SRC_OVERFLOW;

	uint8_t code = *src++;
	unsigned csize = stenos::read_uint32_3(src);
	unsigned dsize = (unsigned)dst_size;
	src += 3;
	if STENOS_UNLIKELY (src + csize > end_src || dst + dsize > end_dst)
		return STENOS_ERROR_INVALID_INPUT;
	if STENOS_UNLIKELY (stenos::has_error(ctx->ensure_has_buffers(1)))
		return STENOS_ERROR_ALLOC;
	return stenos::decompress_generic_superblock(ctx, code, src, bytesoftype, csize, dst, dsize, ctx->tmp_buffers1[0]);
}

size_t stenos_private_block_size(const void* _src, size_t src_size)
{
	// Private API used by cvector, returns the superblock compressed size
	if (src_size < 4)
		return STENOS_ERROR_SRC_OVERFLOW;
	const uint8_t* src = (const uint8_t*)_src;
	//uint8_t code = *src++;
	++src; // skip code
	unsigned csize = stenos::read_uint32_3(src);
	return csize + 4;
}

size_t stenos_private_create_compression_header(size_t decompressed_size, size_t super_block_size, void* _dst, size_t dst_size)
{
	// Private API used by cvector, create frame header
	if (dst_size < 12)
		return STENOS_ERROR_DST_OVERFLOW;
	uint8_t* dst = (uint8_t*)_dst;
	*dst++ = 255; // 255 is for custom super block size
	stenos::write_uint64_7(dst, decompressed_size);
	dst += 7;
	// write block size
	stenos::write_LE_32(dst, (unsigned)super_block_size);
	dst += 4;
	return (dst - (uint8_t*)_dst);
}



size_t stenos_compress_generic(stenos_context* opts, const void* _src, size_t bytesoftype, size_t bytes, void* _dst, size_t dst_size)
{
	// Public API, generic compression function

	// Prepare the context for compression
	size_t prep = opts->prepare(bytesoftype, bytes);
	if STENOS_UNLIKELY (stenos::has_error(prep))
		return prep;

	// Compute number of superblocks
	size_t super_block_remaining = bytes % opts->superblock_size;
	size_t super_block_count = bytes / opts->superblock_size + (super_block_remaining ? 1 : 0);
	uint8_t* dst = (uint8_t*)_dst;
	uint8_t* dst_end = dst + dst_size;
	const uint8_t* src = (const uint8_t*)_src;
	const uint8_t* src_end = src + bytes;

	// Write shift and uncompressed size
	if STENOS_UNLIKELY (dst + 8 > dst_end)
		return STENOS_ERROR_DST_OVERFLOW;
	*dst++ = (uint8_t)opts->shift;
	stenos::write_uint64_7(dst, bytes);
	dst += 7;

	if (opts->custom_blocksize_shift != STENOS_NO_BLOCK_SHIFT) {
		// Write custom superblock size
		if STENOS_UNLIKELY (dst + 4 > dst_end)
			return STENOS_ERROR_DST_OVERFLOW;
		stenos::write_LE_32(dst, (unsigned)opts->superblock_size);
		dst += 4;
	}

	// Check for null input
	if STENOS_UNLIKELY (bytes == 0)
		return (size_t)(dst - (uint8_t*)_dst);

	// Check for overflow
	if STENOS_UNLIKELY (dst > dst_end)
		return STENOS_ERROR_DST_OVERFLOW;

	if (opts->threads <= 1 || super_block_count == 1) {

		// Mono thread compression

		// Create buffers
		if STENOS_UNLIKELY (stenos::has_error(opts->ensure_has_buffers(1)))
			return STENOS_ERROR_ALLOC;

		// Loop over blocks
		for (size_t i = 0; i < super_block_count; ++i) {
			size_t in_size = (i == super_block_count - 1) ? (size_t)(src_end - src) : opts->superblock_size;
			size_t r = stenos::compress_generic_superblock(opts, src, bytesoftype, in_size, dst, (dst_end - dst), opts->tmp_buffers1[0], opts->tmp_buffers2[0]);

			if (stenos::has_error(r))
				return r;
			if (opts->t.nanoseconds)
				// Update compressed bytes
				opts->t.processed_bytes.fetch_add(in_size);
			src += opts->superblock_size;
			dst += r;
		}

		return dst - (uint8_t*)_dst;
	}

	// Multithread compression

	const bool raw_memcpy = !opts->t.nanoseconds && opts->level == 0;				// Check for direct memcpy (level 0)
	const int threads = (int)std::min((size_t)opts->threads, super_block_count);	// Compute number of threads
	size_t res_code = 0;

	if (!raw_memcpy) {
		if STENOS_UNLIKELY (stenos::has_error(opts->ensure_has_buffers(threads)))
			return STENOS_ERROR_ALLOC;
	}
	else {
		// Compute output size
		size_t out_size = super_block_count * 4 + (super_block_count - 1) * opts->superblock_size + super_block_remaining;
		if STENOS_UNLIKELY (dst + out_size > dst_end)
			return STENOS_ERROR_DST_OVERFLOW;
	}

	size_t chunks = super_block_count;
	while (chunks) {

		int thread_count = threads;
		if ((size_t)thread_count > chunks)
			thread_count = (int)chunks;

		std::atomic<size_t> memcpy_size{ 0 };

		// Parallel compression using a thread pool
		for (int i = 0; i < thread_count; ++i) {
			if (!stenos::pool->push([&, i]() {
				    size_t idx = (size_t)i;
				    // Get input pointer
				    const uint8_t* in = src + idx * opts->superblock_size;

				    if (raw_memcpy) {
						// Direct memcpy
					    size_t in_size = (size_t)(src_end - in) < opts->superblock_size ? (size_t)(src_end - in) : opts->superblock_size;
					    uint8_t* out = dst + idx * (opts->superblock_size + 4);
					    stenos::compress_memcpy(in, in_size, out, dst_end - out);
					    memcpy_size.fetch_add(in_size);
				    }
				    else {
					    // Retrieve a compression buffer
					    auto* buffer = opts->thread_buffers[idx];
					    if (!buffer)
						    buffer = opts->thread_buffers[idx] = stenos::CBuffer::make(opts->superblock_size + 4); // Add 4 for the superblock header
					    if (buffer) {
						    // Compress with computed level
						    size_t in_size = (size_t)(src_end - in) < opts->superblock_size ? (size_t)(src_end - in) : opts->superblock_size;
						    buffer->dst_size = stenos::compress_generic_superblock(
						      opts, in, bytesoftype, in_size, buffer->bytes, opts->superblock_size + 4, opts->tmp_buffers1[idx], opts->tmp_buffers2[idx]);
						    if (opts->t.nanoseconds)
							    opts->t.processed_bytes.fetch_add(in_size);
					    }
				    }
			    }))
				return STENOS_ERROR_ALLOC;
		}
		stenos::pool->wait();

		if (raw_memcpy) {
			src += memcpy_size.load();
			dst += memcpy_size.load() + (size_t)thread_count * 4;
			if (dst > dst_end)
				return STENOS_ERROR_DST_OVERFLOW;
		}
		else {

			// Write compressed size to destination and check errors
			for (size_t i = 0; i < (size_t)thread_count; ++i) {
				if STENOS_UNLIKELY (!opts->thread_buffers[i]) {
					res_code = STENOS_ERROR_ALLOC;
					goto end;
				}
				if STENOS_UNLIKELY (stenos::has_error(opts->thread_buffers[i]->dst_size)) {
					res_code = STENOS_ERROR_DST_OVERFLOW;
					goto end;
				}

				opts->thread_buffers[i]->dst = dst;
				dst += opts->thread_buffers[i]->dst_size;
				src += opts->superblock_size;
			}

			// copy to destination in parallel
			for (int i = 0; i < thread_count; ++i) {
				if (!stenos::pool->push([&, i]() {
					    if ((uint8_t*)opts->thread_buffers[i]->dst + opts->thread_buffers[i]->dst_size > dst_end) {
						    res_code = STENOS_ERROR_DST_OVERFLOW;
					    }
					    else {
						    memcpy(opts->thread_buffers[i]->dst, opts->thread_buffers[i]->bytes, opts->thread_buffers[i]->dst_size);
					    }
				    }))
					return STENOS_ERROR_ALLOC;
			}
			stenos::pool->wait();

			if STENOS_UNLIKELY (res_code)
				goto end;
		}
		chunks -= (size_t)thread_count;
	}

end:
	if (!res_code)
		res_code = dst - (uint8_t*)_dst;

	return res_code;
}

size_t stenos_get_info(const void* _src, size_t bytesoftype, size_t bytes, stenos_info* info)
{
	// Retrieve information on a compressed frame

	const uint8_t* src = (const uint8_t*)_src;
	const uint8_t* end_src = src + bytes;

	if STENOS_UNLIKELY (src + 8 > end_src)
		return STENOS_ERROR_SRC_OVERFLOW;

	uint8_t shift = *src++;
	if STENOS_UNLIKELY (shift > 4 && shift != 255)
		return STENOS_ERROR_INVALID_INPUT;

	// Decompressed size
	info->decompressed_size = (size_t)stenos::read_uint64_7(src);
	src += 7;

	// Superblock size
	if (shift == 255) {
		if (src + 4 > end_src)
			return STENOS_ERROR_SRC_OVERFLOW;
		info->superblock_size = stenos::read_LE_32(src);
		src += 4;
	}
	else {
		info->superblock_size = stenos::super_block_size(bytesoftype * 256) << shift;
	}

	// Returns the frame header size
	return (size_t)(src - (const uint8_t*)_src);
}

size_t stenos_decompress_generic(stenos_context* opts, const void* _src, size_t bytesoftype, size_t size, void* _dst, size_t dst_size)
{
	// Public API, generic decompression

	struct Block
	{
		unsigned csize;
		unsigned dsize;
		uint8_t code;
		const uint8_t* src;
		uint8_t* dst;
		size_t ret;
	};

	// Check bytesoftype validity
	if STENOS_UNLIKELY (bytesoftype == 0 || bytesoftype >= STENOS_MAX_BYTESOFTYPE)
		return STENOS_ERROR_INVALID_BYTESOFTYPE;

	const uint8_t* src = (const uint8_t*)_src;
	const uint8_t* end_src = src + size;
	uint8_t* dst = (uint8_t*)_dst;
	uint8_t* end_dst = dst + dst_size;
	uint64_t decompressed = dst_size;
	uint8_t shift = 0;

	// Check overflow
	if STENOS_UNLIKELY (src + 8 > end_src)
		return STENOS_ERROR_SRC_OVERFLOW;
	shift = *src++;

	// Check shift validity
	if STENOS_UNLIKELY (shift > 4 && shift != 255)
		return STENOS_ERROR_INVALID_INPUT;

	// Read decompressed size
	decompressed = stenos::read_uint64_7(src);
	src += 7;
	if STENOS_UNLIKELY (decompressed > dst_size)
		return STENOS_ERROR_DST_OVERFLOW;
	if (decompressed == 0)
		return 0;

	// Compute superblock size
	size_t block_size = bytesoftype * 256;
	size_t superblock_size = 0;

	if (shift == 255) {
		// Custom superblock size
		if STENOS_UNLIKELY (src + 4 > end_src)
			return STENOS_ERROR_SRC_OVERFLOW;
		superblock_size = stenos::read_LE_32(src);
		src += 4;
	}
	else
		// Default superblock size
		superblock_size = stenos::super_block_size(block_size) << shift;

	// Clear buffers
	if (superblock_size != opts->superblock_size)
		opts->clear_buffers();
	opts->superblock_size = superblock_size;

	// Compute superblock count
	size_t super_block_remaining = decompressed % opts->superblock_size;
	size_t super_block_count = decompressed / opts->superblock_size + (super_block_remaining ? 1 : 0);

	if (opts->threads <= 1 || super_block_count == 1) {
		// Mono thread decompression
		if STENOS_UNLIKELY (stenos::has_error(opts->ensure_has_buffers(1)))
			return STENOS_ERROR_ALLOC;

		// Loop over superblocks
		for (size_t i = 0; i < super_block_count; ++i) {

			if STENOS_UNLIKELY (src + 4 > end_src)
				return STENOS_ERROR_SRC_OVERFLOW;

			uint8_t code = *src++;
			unsigned csize = stenos::read_uint32_3(src);
			unsigned dsize = (i == super_block_count - 1) ? (unsigned)super_block_remaining : (unsigned)opts->superblock_size;
			src += 3;
			if STENOS_UNLIKELY (src + csize > end_src || dst + dsize > end_dst)
				return STENOS_ERROR_INVALID_INPUT;

			size_t ret = stenos::decompress_generic_superblock(opts, code, src, bytesoftype, csize, dst, dsize, opts->tmp_buffers1[0]);
			if STENOS_UNLIKELY (ret != dsize)
				// Error
				return ret;

			dst += dsize;
			src += csize;
		}

		size_t output_size = dst - (uint8_t*)_dst;
		if STENOS_UNLIKELY (output_size != decompressed)
			return STENOS_ERROR_INVALID_INPUT;
		return decompressed;
	}

	// Multithread decompression

	const int threads = (int)std::min((size_t)opts->threads, super_block_count);
	std::vector<Block> blocks((size_t)threads);
	if STENOS_UNLIKELY (stenos::has_error(opts->ensure_has_buffers(threads)))
		return STENOS_ERROR_ALLOC;

	size_t chunks = super_block_count;
	while (chunks) {

		int thread_count = threads;
		if ((size_t)thread_count > chunks)
			thread_count = (int)chunks;

		// Build blocks;
		for (int i = 0; i < thread_count; ++i) {

			if STENOS_UNLIKELY (src + 4 >= end_src)
				return STENOS_ERROR_SRC_OVERFLOW;

			uint8_t code = *src++;
			unsigned csize = stenos::read_uint32_3(src);
			unsigned dsize = (chunks - (size_t)i - 1 == 0) ? (unsigned)super_block_remaining : (unsigned)opts->superblock_size;
			src += 3;
			if STENOS_UNLIKELY (src + csize > end_src || dst + dsize > end_dst)
				return STENOS_ERROR_INVALID_INPUT;

			blocks[(size_t)i] = Block{ csize, dsize, code, src, dst };

			dst += dsize;
			src += csize;
		}

		// Parallel decompress using a thread pool
		for (int i = 0; i < thread_count; ++i) {
			if (!stenos::pool->push([&, i]() {
				    Block& bl = blocks[(size_t)i];
				    bl.ret = stenos::decompress_generic_superblock(opts, bl.code, bl.src, bytesoftype, bl.csize, bl.dst, bl.dsize, opts->thread_buffers[(size_t)i]);
			    }))
				return STENOS_ERROR_ALLOC;
		}
		stenos::pool->wait();

		// Check results
		for (int i = 0; i < thread_count; ++i) {
			Block& bl = blocks[(size_t)i];
			if STENOS_UNLIKELY (bl.ret != bl.dsize)
				return bl.ret;
		}

		chunks -= (size_t)thread_count;
	}

	size_t output_size = dst - (uint8_t*)_dst;
	if STENOS_UNLIKELY (output_size != decompressed)
		return STENOS_ERROR_INVALID_INPUT;
	return decompressed;
}

size_t stenos_compress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size, int level)
{
	// Public API, simplified compression function only using a compression level as parameter.

	// Create the context on the stack
	stenos_context_s opts;
	opts.level = level > 9 ? 9 : (level < 0 ? 0 : level);
	return stenos_compress_generic(&opts, src, bytesoftype, bytes, dst, dst_size);
}
size_t stenos_decompress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size)
{
	// Public API, simplified decompression function.

	// Create the context on the stack
	stenos_context_s opts;
	return stenos_decompress_generic(&opts, src, bytesoftype, bytes, dst, dst_size);
}





//
// C Timer structure wrapping stenos::timer
//

struct stenos_timer_s
{
	stenos::timer t;
};
stenos_timer* stenos_make_timer()
{
	stenos_timer* t = (stenos_timer*)malloc(sizeof(stenos_timer));
	if STENOS_UNLIKELY (!t)
		return nullptr;
	return new (t) stenos_timer();
}
void stenos_destroy_timer(stenos_timer* timer)
{
	if (timer) {
		timer->~stenos_timer();
		free(timer);
	}
}
void stenos_tick(stenos_timer* timer)
{
	timer->t.tick();
}
uint64_t stenos_tock(stenos_timer* timer)
{
	return timer->t.tock();
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// TEST
/*#include <fstream>
#include <iostream>
int main(int, char**)
{
	std::ifstream fin("C:/Users/VM213788/Desktop/results.data", std::ios::binary);
	fin.seekg(0, std::ios::end);
	size_t size = fin.tellg();
	fin.seekg(0, std::ios::beg);

	std::vector<char> buf(size);
	fin.read(buf.data(), buf.size());

	std::vector<char> dst(buf.size());
	size_t r = 0;
	for (int i = 0; i < 10; ++i)
		r = stenos_compress(buf.data(), 5, buf.size(), dst.data(), dst.size(), 1);
	std::cout << r << std::endl;
}*/

#include "delta.h"
#include <iostream>

int stenos_private_test(const void* data, size_t bytes)
{
	stenos::timer t;
	for (int i = 0; i <= 10; ++i) {

		t.tick();
		/*double r =*/ stenos::lz4_guess_ratio((char*)data, bytes, 10);
		auto el = t.tock() * 1e-9;
		std::cout << (bytes / el) / 1000000000 << std::endl;
	}
	return 0;
}

int stenos_private_test_delta()
{
	{
		char vec[127];
		for (int i = 0; i < 127; ++i)
			vec[i] = i + 1;
		char dst[127];
		char vec2[127];
		stenos::delta(vec, dst, 127);

		stenos::delta_inv(dst, vec2, 127);
		//bool stop = true;
	}
	std::vector<char> src(9999);
	for (size_t i = 0; i < src.size(); ++i)
		src[i] = rand();

	auto dst = src;

	stenos::timer t;
	t.tick();

	size_t mult = 1000000;
	for (size_t i = 0; i < mult; ++i)
		stenos::delta(src.data(), dst.data(), src.size());

	auto el = t.tock() * 1e-9;
	std::cout << "delta " << (((src.size() * mult)) / el) / 1000000000 << " GB/s" << std::endl;

	auto d2 = dst;

	t.tick();
	for (size_t i = 0; i < mult; ++i)
		stenos::delta_inv(dst.data(), d2.data(), d2.size());

	el = t.tock() * 1e-9;
	std::cout << "delta inv " << (((src.size() * mult)) / el) / 1000000000 << " GB/s" << std::endl;

	return memcmp(src.data(), d2.data(), d2.size());
}
