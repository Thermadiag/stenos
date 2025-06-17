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

#ifndef STENOS_ZSTD_BUFFER_H
#define STENOS_ZSTD_BUFFER_H

#include <atomic>
#include <thread>
#include <zstd.h>
#include <zstd_errors.h>
#include "../bits.hpp"
#include "../stenos.h"



namespace stenos
{
	// Small class gathering information on a time constraint
	struct TimeConstraint
	{
		timer timer;								// Timer
		uint64_t nanoseconds{ 0 };					// Maximum nanoseconds
		uint64_t total_bytes{ 0 };					// Total number of bytes to compress
		std::atomic<uint64_t> processed_bytes{ 0 }; // Currently processed bytes
		std::atomic<bool> finish_memcpy{ false };   // Should we finish with memcpy
	};

	// Convert stenos level to zstd level
	static STENOS_ALWAYS_INLINE int zstd_from_reduced_level(int clevel) noexcept
	{
		if (clevel < 1)
			return 1;
		if (clevel < 9)
			return clevel * 2 - 1;
		return ZSTD_maxCLevel();
	}

	// ZSTD compression with optional dictionary
	static STENOS_ALWAYS_INLINE size_t zstd_compress_with_context(void* dst, size_t dstCapacity, const void* src, size_t srcSize, int level, ZSTD_CDict* dict = nullptr) noexcept
	{
		// Compress by reusing a thread local ZSTD_CCtx
		struct ZSTDContext
		{
			ZSTD_CCtx* ctx{ nullptr };
			ZSTDContext()
			  : ctx(ZSTD_createCCtx())
			{
			}
			~ZSTDContext()
			{
				if (ctx)
					ZSTD_freeCCtx(ctx);
			}
		};
		thread_local ZSTDContext ctx;
		if (!ctx.ctx)
			return STENOS_ERROR_ALLOC;

		size_t r = 0;
		if (dict)
			r = ZSTD_compress_usingCDict(ctx.ctx, dst, dstCapacity, src, srcSize, dict);
		else
			r = ZSTD_compressCCtx(ctx.ctx, dst, dstCapacity, src, srcSize, zstd_from_reduced_level(level));

		if (ZSTD_getErrorCode(r) == ZSTD_error_dstSize_tooSmall)
			return STENOS_ERROR_DST_OVERFLOW;
		if (ZSTD_isError(r))
			return STENOS_ERROR_ZSTD_INTERNAL;
		return r;
	}

	namespace detail
	{

		static STENOS_ALWAYS_INLINE const std::pair<unsigned, unsigned>* compress_rates() noexcept
		{
			// Returns an estimated compression rate in bytes/s for zstd
			static const std::pair<unsigned, unsigned> rates[9] = { { 2000000, 9 },	 { 5000000, 8 },  { 7000000, 7 },   { 9000000, 6 },   { 20000000, 5 },
										{ 40000000, 4 }, { 60000000, 3 }, { 230000000, 2 }, { 300000000u, 1 } };
			return rates;
		}

		static STENOS_ALWAYS_INLINE int level_for_rate(const std::pair<unsigned, unsigned>* rates, size_t rate, unsigned shift) noexcept
		{
			// Returns the best compression level (0 to 9) based on provided rate.
			// 0 means to use memcpy directly.
			auto it = std::lower_bound(rates, rates + 9, rate, [shift](const auto& l, const auto& r) { return (l.first << shift) < r; });
			if (it == rates + 9)
				return rate > ((rates[8].first << shift) * 1.5) ? 0 : 1;
			return it->second;
		}

		static STENOS_ALWAYS_INLINE int level_for_rate(size_t rate, unsigned shift) noexcept
		{
			return level_for_rate(compress_rates(), rate, shift);
		}

		static inline int clevel_for_remaining(TimeConstraint& t, size_t processed_bytes, size_t* target_rate = nullptr, unsigned shift = 0) noexcept
		{
			// Compute the best possible compressoin level for remaining bytes
			// based on the time constraint and the current compression rate

			int clevel = 0;

			auto el = t.timer.tock();
			size_t remaining_bytes = t.total_bytes - processed_bytes;

			auto memcpy_time_ns = remaining_bytes / 16;
			if (el + memcpy_time_ns > t.nanoseconds) {
				// Finish with memcpy
				t.finish_memcpy.store(true);
				return clevel;
			}

			{
				size_t rate = target_rate ? *target_rate : ((size_t)(remaining_bytes / ((t.nanoseconds - el) * 1.e-9)));
				clevel = detail::level_for_rate(rate, shift);
				if (processed_bytes == 0)
					return clevel < 1 ? 1 : clevel;

				if (clevel > 6)
					clevel = 6; // Use levels > 6 only if time advance is low
				// adjust level based on current advance
				int prev = clevel;

				double advance = processed_bytes / (double)t.total_bytes;
				double advance_time = el / (double)t.nanoseconds;
				if (advance > advance_time * 1.3) {
					clevel += 1 + (advance > advance_time * 1.6) + (advance > advance_time * 2);
				}
				else if (advance < advance_time) {
					clevel -= 1 + (advance * 1.6 < advance_time);
				}

				if (clevel < 1 && !target_rate) {
					double factor = 0.5 + (1 - (double)remaining_bytes / (double)t.total_bytes) * 0.5;
					if (advance > advance_time * factor)
						clevel = 1;
				}
			}

			return clevel;
		}

	}
}

#endif
