/*
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

#ifndef STENOS_CONTEXT_H
#define STENOS_CONTEXT_H

#include "../bits.hpp"
#include "block_compress.h"
#include "zstd_wrapper.h"

#include <vector>
#include <cstdint>
#include <cstddef>
#include <atomic>


namespace stenos
{
	/// @brief Compression/decompression buffer class
	/// used by steno_compress/decompress functions
	struct CBuffer
	{
		// Additional infos
		void* dst{ nullptr };
		std::size_t dst_size{ 0 };

		// Compression buffer
		char* bytes{ nullptr };

		static CBuffer* make(std::size_t bytes) noexcept
		{
			std::size_t alloc = bytes + 16 + sizeof(CBuffer); // Add 16 to ensure aligned access
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
	static STENOS_ALWAYS_INLINE std::size_t super_block_size(std::size_t block_size) noexcept
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
	std::size_t superblock_size{ 0 };

	// Time constraint
	stenos::TimeConstraint t;

	// Parameters
	int threads{ 1 };
	int level{ 1 };
	int shift{ 0 };
	std::size_t custom_blocksize_shift{ STENOS_NO_BLOCK_SHIFT };
	double max_error{ 0 };
	std::size_t stride{ 1 };

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

	std::size_t prepare(std::size_t bytesoftype, std::size_t bytes) noexcept
	{
		// Prepare the compression of given number of bytes

		if STENOS_UNLIKELY (bytesoftype == 0 || bytesoftype >= STENOS_MAX_BYTESOFTYPE)
			return STENOS_ERROR_INVALID_BYTESOFTYPE;

		std::size_t block_size = bytesoftype * 256;
		std::size_t new_superblock_size = 0;
		shift = 0;

		if (t.nanoseconds) {
			// try to have at least thread * 32 super block
			std::size_t required_superblock_count = threads * 32;
			std::size_t required_superblock_size = bytes / required_superblock_count;

			std::size_t block_count = required_superblock_size / block_size;
			if (block_count == 0)
				block_count = 1;

			new_superblock_size = block_size * block_count;
			shift = 255;
			if (new_superblock_size >= STENOS_MAX_BLOCK_BYTES) {
				// Default strategy for level 9
				new_superblock_size = stenos::super_block_size(block_size);
				if (bytes > new_superblock_size) {
					shift = (9 - 1) / 2;
					new_superblock_size = new_superblock_size << (std::size_t)shift;
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
					new_superblock_size = new_superblock_size << (std::size_t)shift;
					if (threads > 1) {
						// Adjust to threads
						while (shift > 0 && (bytes / new_superblock_size + 1) < (std::size_t)threads) {
							--shift;
							new_superblock_size /= 2;
						}
					}
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

	STENOS_ALWAYS_INLINE std::size_t ensure_has_buffers(int size) noexcept
	{
		// Ensure that we have the requested number of buffers
		try {
			if (thread_buffers.size() < (std::size_t)size) {
				thread_buffers.resize((std::size_t)size, nullptr);
				tmp_buffers1.resize((std::size_t)size, nullptr);
				tmp_buffers2.resize((std::size_t)size, nullptr);
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

		for (std::size_t i = 0; i < thread_buffers.size(); ++i) {
			if (thread_buffers[i]) {
				stenos::CBuffer::destroy(thread_buffers[i]);
				thread_buffers[i] = nullptr;
			}
		}
		thread_buffers.clear();
		for (std::size_t i = 0; i < tmp_buffers1.size(); ++i) {
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


#endif
