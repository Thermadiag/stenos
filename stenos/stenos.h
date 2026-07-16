/**
 * @file stenos.h
 * @brief Public C and C++ API for the Stenos generic compression library.
 *
 * Stenos compresses arrays of fixed-size binary elements. Depending on the
 * selected compression level and on the input data, the encoder may combine
 * byte transposition, delta coding, block compression, and raw Zstandard.
 *
 * The library exposes a C ABI. C++ users additionally get the constexpr
 * stenos::compress_bound() helper.
 *
 * @copyright
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

#ifndef STENOS_H
#define STENOS_H

#ifdef __cplusplus
#include <cstddef>
#include <limits>

/** @brief C++ convenience functions for Stenos. */
namespace stenos
{
	/**
	 * @brief Return an upper bound for the compressed size of an input buffer.
	 *
	 * This constexpr helper is equivalent to stenos_bound() and can be used in
	 * compile-time C++ expressions.
	 *
	 * @param bytes Number of uncompressed input bytes.
	 * @return A destination size that is sufficient for any valid Stenos frame
	 *         produced from @p bytes input bytes.
	 */
	inline constexpr size_t compress_bound(size_t bytes)
	{
		constexpr size_t min_superblock_size = 65792; /* Minimum superblock size, for bytesoftype 257*/
		const size_t count = bytes / min_superblock_size + (bytes % min_superblock_size != 0);

		const size_t headers = (count == 0 ? 1 : count);

		if (headers > (std::numeric_limits<size_t>::max() - 12 - bytes) / 4)
			return std::numeric_limits<size_t>::max();

		return 12 + headers * 4 + bytes;
	}
}

extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
}
#endif

#include "stenos_export.h"

/** @defgroup stenos_constants Format and configuration constants */
/** @{ */

/** Default target superblock size, in bytes. */
#define STENOS_BLOCK_SIZE (131072)

/**
 * Maximum supported superblock size, in bytes.
 *
 * Superblock sizes stored in the frame format must remain strictly below this
 * value.
 */
#define STENOS_MAX_BLOCK_BYTES ((1u << 24u) - 1u)

/**
 * Exclusive upper bound for the size of one logical element, in bytes.
 *
 * Valid values satisfy:
 * @code
 * 0 < bytesoftype && bytesoftype < STENOS_MAX_BYTESOFTYPE
 * @endcode
 */
#define STENOS_MAX_BYTESOFTYPE (STENOS_MAX_BLOCK_BYTES / 256)

/**
 * Sentinel passed to stenos_set_block_size() to restore automatic superblock
 * sizing.
 */
#define STENOS_NO_BLOCK_SHIFT ((size_t)-1)

/** @} */

/** @defgroup stenos_errors Error codes */
/** @{ */

/** Unspecified internal failure. */
#define STENOS_ERROR_UNDEFINED ((size_t)(-1))
/** The input buffer or stream ended before the requested data was available. */
#define STENOS_ERROR_SRC_OVERFLOW ((size_t)(-2))
/** A memory allocation failed. */
#define STENOS_ERROR_ALLOC ((size_t)(-3))
/** The compressed input or a supplied argument contains invalid data. */
#define STENOS_ERROR_INVALID_INPUT ((size_t)(-4))
/** The current CPU does not provide a required instruction set. */
#define STENOS_ERROR_INVALID_INSTRUCTION_SET ((size_t)(-5))
/** The destination buffer is too small. */
#define STENOS_ERROR_DST_OVERFLOW ((size_t)(-6))
/** The supplied element size is outside the supported range. */
#define STENOS_ERROR_INVALID_BYTESOFTYPE ((size_t)(-7))
/** An internal Zstandard operation failed. */
#define STENOS_ERROR_ZSTD_INTERNAL ((size_t)(-8))
/** A configuration or function parameter is invalid. */
#define STENOS_ERROR_INVALID_PARAMETER ((size_t)(-9))
/** A stream callback failed or returned an invalid result. */
#define STENOS_ERROR_INVALID_IO ((size_t)(-10))
/** A supplied filename is invalid. */
#define STENOS_ERROR_INVALID_FILENAME ((size_t)(-11))
/** Reserved lower bound for future error codes. */
#define STENOS_LAST_ERROR_CODE ((size_t)(-100))

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup stenos_context_api Compression and decompression contexts */
/** @{ */

/**
 * @brief Opaque compression and decompression context.
 *
 * A context stores compression parameters and reusable temporary buffers.
 * Reusing a context across calls generally avoids repeated allocations.
 *
 * A single context must not be used concurrently by multiple threads unless
 * external synchronization is provided. Different contexts may be used in
 * parallel.
 */
typedef struct stenos_context_s stenos_context;

/** Seek from the beginning of the input stream. */
#define STENOS_SEEK_SET 0
/** Seek relative to the current input-stream position. */
#define STENOS_SEEK_CUR 1

/**
 * @brief Read-only stream interface used by stenos_decompress_sub_part().
 *
 * Each callback receives the value stored in @ref opaque. A callback reports
 * failure by returning a negative value.
 *
 * The @ref read callback must return the number of bytes actually copied to
 * the destination. The @ref seek callback must return zero on success. The
 * @ref tell callback must return the current absolute byte position.
 */
typedef struct stenos_input_s
{
	/** Read at most the requested number of bytes into the destination. */
	int64_t (*read)(char* dst, int64_t bytes, void* opaque);
	/** Move the current stream position using STENOS_SEEK_SET or STENOS_SEEK_CUR. */
	int64_t (*seek)(int64_t offset, int whence, void* opaque);
	/** Return the current absolute byte position in the stream. */
	int64_t (*tell)(void* opaque);
	/** User-provided object forwarded unchanged to all callbacks. */
	void* opaque;
} stenos_input;

/**
 * @brief Optional lock interface used by the video decompression API.
 *
 * See @c stenos_video.h for the functions that consume this structure.
 */
typedef struct stenos_lock_s
{
	/** Acquire the user-provided lock. */
	void (*lock)(void* opaque);
	/** Release the user-provided lock. */
	void (*unlock)(void* opaque);
	/** User-provided lock object forwarded to both callbacks. */
	void* opaque;
} stenos_lock;

/**
 * @brief Create a compression/decompression context.
 *
 * The new context uses compression level 1, one worker thread, automatic
 * superblock sizing, and no compression-time limit.
 *
 * @return A newly allocated context, or NULL if allocation fails.
 * @see stenos_destroy_context()
 */
STENOS_EXPORT stenos_context* stenos_make_context();

/**
 * @brief Destroy a context and release all memory owned by it.
 *
 * Passing NULL is allowed and has no effect.
 *
 * @param ctx Context to destroy.
 */
STENOS_EXPORT void stenos_destroy_context(stenos_context* ctx);

/**
 * @brief Restore a context's default compression parameters.
 *
 * Previously allocated temporary buffers may remain available for reuse.
 *
 * @param ctx Context to reset. Passing NULL has no effect.
 */
STENOS_EXPORT void stenos_reset_context(stenos_context* ctx);

/**
 * @brief Set the compression level.
 *
 * Level 0 stores the input without compression. Levels 1 through 9 trade
 * compression speed for a generally better compression ratio. Values below 0
 * are clamped to 0 and values above 9 are clamped to 9.
 *
 * @param ctx Context to configure.
 * @param level Requested compression level.
 * @return 0 on success, or a Stenos error code.
 */
STENOS_EXPORT size_t stenos_set_level(stenos_context* ctx, int level);

/**
 * @brief Set the maximum number of worker threads.
 *
 * Values below 1 are treated as 1. Values above the detected hardware
 * concurrency may be clamped.
 *
 * @param ctx Context to configure.
 * @param threads Requested number of worker threads.
 * @return 0 on success, or a Stenos error code.
 */
STENOS_EXPORT size_t stenos_set_threads(stenos_context* ctx, int threads);

/**
 * @brief Set a soft upper bound on compression time.
 *
 * When @p nanoseconds is non-zero, the compressor dynamically adjusts its work
 * to try to finish within the requested duration. If the remaining time is too
 * short, subsequent blocks may be stored without compression. The limit is
 * best-effort and its precision depends on the platform timer and scheduler.
 *
 * A value of zero disables time-bounded compression.
 *
 * @param ctx Context to configure.
 * @param nanoseconds Requested maximum duration, in nanoseconds.
 * @return 0 on success, or a Stenos error code.
 */
STENOS_EXPORT size_t stenos_set_max_nanoseconds(stenos_context* ctx, uint64_t nanoseconds);

/**
 * @brief Select a custom superblock size.
 *
 * The effective superblock size is computed as:
 * @code
 * (bytesoftype * 256) << blocksize_shift
 * @endcode
 *
 * Pass STENOS_NO_BLOCK_SHIFT to restore automatic sizing. This option is mainly
 * intended to deserialize data produced by @c stenos::cvector; in that case it
 * must match the container's @c BlockSize template argument.
 *
 * @param ctx Context to configure.
 * @param blocksize_shift Shift applied to the base block size, or
 *        STENOS_NO_BLOCK_SHIFT.
 * @return 0 on success, or STENOS_ERROR_INVALID_PARAMETER if the requested
 *         shift cannot produce a valid superblock size.
 */
STENOS_EXPORT size_t stenos_set_block_size(stenos_context* ctx, size_t blocksize_shift);

/**
 * @brief Estimate the memory currently owned by a context.
 *
 * @param ctx Context to inspect.
 * @return Approximate memory footprint, in bytes.
 */
STENOS_EXPORT size_t stenos_memory_footprint(stenos_context* ctx);

/** @} */

/** @defgroup stenos_compression_api Compression and decompression */
/** @{ */

/**
 * @brief Test whether a size_t result is a Stenos error code.
 *
 * @param result Return value obtained from a Stenos function.
 * @return Non-zero if @p result represents an error; zero otherwise.
 */
STENOS_EXPORT int stenos_has_error(size_t result);

/**
 * @brief Return an upper bound for the compressed size of an input buffer.
 *
 * Allocate at least the returned number of bytes before calling a compression
 * function.
 *
 * @param bytes Number of uncompressed input bytes.
 * @return Required destination capacity, in bytes.
 */
STENOS_EXPORT size_t stenos_bound(size_t bytes);

/**
 * @brief Compress an array of fixed-size binary elements using a context.
 *
 * The input contains @p bytes total bytes, arranged as elements of
 * @p bytesoftype bytes each. For structured data, @p bytesoftype should match
 * the complete logical record size so that byte transposition groups equal
 * byte positions together.
 *
 * @param ctx Compression context.
 * @param src Input buffer. May be NULL only when @p bytes is zero.
 * @param bytesoftype Size of one logical element, in bytes. Must satisfy
 *        `0 < bytesoftype < STENOS_MAX_BYTESOFTYPE`.
 * @param bytes Total input size, in bytes. Normally a multiple of
 *        @p bytesoftype.
 * @param dst Destination buffer.
 * @param dst_size Capacity of @p dst, in bytes.
 * @return Number of bytes written to @p dst, or a Stenos error code.
 *
 * @warning The input and output memory ranges must not overlap.
 * @see stenos_bound()
 * @see stenos_decompress_generic()
 */
STENOS_EXPORT size_t stenos_compress_generic(stenos_context* ctx, const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);

/**
 * @brief Decompress a Stenos frame using a reusable context.
 *
 * @p bytesoftype must be identical to the value used during compression.
 * @p dst_size is the available output capacity; the exact decompressed size can
 * be obtained beforehand with stenos_get_info().
 *
 * @param ctx Decompression context.
 * @param src Compressed Stenos frame.
 * @param bytesoftype Size of one logical element, in bytes.
 * @param bytes Number of compressed input bytes available at @p src.
 * @param dst Destination buffer.
 * @param dst_size Capacity of @p dst, in bytes.
 * @return Number of decompressed bytes, or a Stenos error code.
 *
 * @warning The input and output memory ranges must not overlap.
 * @see stenos_compress_generic()
 * @see stenos_get_info()
 */
STENOS_EXPORT size_t stenos_decompress_generic(stenos_context* ctx, const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);

/**
 * @brief Compress an array without explicitly creating a context.
 *
 * This convenience function uses the requested compression level and manages
 * its temporary context internally.
 *
 * @param src Input buffer.
 * @param bytesoftype Size of one logical element, in bytes.
 * @param bytes Total input size, in bytes.
 * @param dst Destination buffer.
 * @param dst_size Capacity of @p dst, in bytes.
 * @param level Compression level from 0 to 9. Out-of-range values are clamped.
 * @return Number of bytes written, or a Stenos error code.
 *
 * @warning The input and output memory ranges must not overlap.
 */
STENOS_EXPORT size_t stenos_compress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size, int level);

/**
 * @brief Decompress a Stenos frame without explicitly creating a context.
 *
 * @param src Compressed Stenos frame.
 * @param bytesoftype Size of one logical element. Must match compression.
 * @param bytes Number of compressed input bytes available at @p src.
 * @param dst Destination buffer.
 * @param dst_size Capacity of @p dst, in bytes.
 * @return Number of decompressed bytes, or a Stenos error code.
 *
 * @warning The input and output memory ranges must not overlap.
 */
STENOS_EXPORT size_t stenos_decompress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);

/**
 * @brief Decompress selected element ranges from a seekable Stenos stream.
 *
 * This function avoids reading and decompressing superblocks that do not
 * intersect the requested ranges. It is useful when only a small part of a
 * large frame is needed, especially when the frame resides on slow storage.
 *
 * @p ranges contains @p range_count pairs of half-open element indices:
 * `[begin, end)`. Indices are expressed in logical elements, not bytes. Ranges
 * must be sorted, non-empty, non-overlapping, and contained in the original
 * decompressed array.
 *
 * Output ranges are concatenated in request order into @p dst; gaps between
 * ranges are not represented in the destination buffer.
 *
 * @param ctx Optional reusable decompression context. May be NULL.
 * @param io Initialized seekable input-stream interface.
 * @param bytesoftype Size of one logical element. Must match compression.
 * @param dst Destination buffer receiving the concatenated selected ranges.
 * @param dst_size Capacity of @p dst, in bytes.
 * @param ranges Array containing `range_count * 2` element indices.
 * @param range_count Number of `[begin, end)` pairs in @p ranges.
 * @return Number of bytes written to @p dst, or a Stenos error code.
 *
 * @warning The compressed stream and destination memory must not overlap.
 */
STENOS_EXPORT size_t stenos_decompress_sub_part(stenos_context* ctx, stenos_input* io, size_t bytesoftype, void* dst, size_t dst_size, size_t* ranges, size_t range_count);

/**
 * @brief Guess a likely logical element size for an input buffer.
 *
 * The heuristic examines byte-position correlations that commonly appear in
 * arrays of binary structures. It returns 1 when no convincing structure is
 * detected; otherwise it typically returns a product of powers of 2, 3, and 5.
 *
 * @param src Input buffer.
 * @param bytes Input size, in bytes.
 * @return Guessed element size, in bytes.
 */
STENOS_EXPORT size_t stenos_guess_bytesoftype(const void* src, size_t bytes);

/** @brief Basic metadata stored in a Stenos frame header. */
typedef struct stenos_info_s
{
	/** Total uncompressed frame size, in bytes. */
	size_t decompressed_size;
	/** Encoded superblock size, in bytes. */
	size_t superblock_size;
} stenos_info;

/**
 * @brief Read frame metadata without decompressing the payload.
 *
 * Only the frame header is inspected. At most 12 input bytes are required,
 * depending on whether the frame uses automatic or custom superblock sizing.
 *
 * @param src Beginning of a compressed Stenos frame.
 * @param bytesoftype Size of one logical element. Must match compression when
 *        automatic superblock sizing was used.
 * @param bytes Number of available input bytes at @p src.
 * @param info Output structure receiving the decoded metadata.
 * @return Number of header bytes consumed, or a Stenos error code.
 */
STENOS_EXPORT size_t stenos_get_info(const void* src, size_t bytesoftype, size_t bytes, stenos_info* info);

/** @} */

/** @defgroup stenos_timer_api High-resolution timer */
/** @{ */

/** @brief Opaque high-resolution timer object. */
typedef struct stenos_timer_s stenos_timer;

/**
 * @brief Create a high-resolution timer.
 * @return A new timer, or NULL if allocation fails.
 */
STENOS_EXPORT stenos_timer* stenos_make_timer();

/**
 * @brief Destroy a timer.
 * @param timer Timer to destroy. Passing NULL is allowed.
 */
STENOS_EXPORT void stenos_destroy_timer(stenos_timer* timer);

/**
 * @brief Reset the timer's reference point to the current time.
 * @param timer Timer to reset.
 */
STENOS_EXPORT void stenos_tick(stenos_timer* timer);

/**
 * @brief Return elapsed time since the most recent stenos_tick() call.
 * @param timer Timer to query.
 * @return Elapsed time, in nanoseconds.
 */
STENOS_EXPORT uint64_t stenos_tock(stenos_timer* timer);

/** @} */

/** @defgroup stenos_private_api Internal cvector integration API */
/**
 * @brief Internal functions used by @c stenos::cvector.
 *
 * These entry points are exported for integration purposes but are not intended
 * as a stable general-purpose public API.
 * @{ */

/** Compress one superblock using an explicitly supplied superblock size. */
STENOS_EXPORT size_t stenos_private_compress_block(stenos_context* ctx, const void* src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* dst, size_t dst_size);

/** Decompress one encoded superblock. */
STENOS_EXPORT size_t stenos_private_decompress_block(stenos_context* ctx, const void* _src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* _dst, size_t dst_size);

/** Return the encoded size of a superblock after validating the available header bytes. */
STENOS_EXPORT size_t stenos_private_block_size(const void* _src, size_t src_size);

/** Return the encoded size of a superblock from its four-byte header. */
STENOS_EXPORT size_t stenos_private_block_csize(const void* _src);

/** Write a Stenos frame header using an explicitly supplied superblock size. */
STENOS_EXPORT size_t stenos_private_create_compression_header(size_t decompressed_size, size_t super_block_size, void* _dst, size_t dst_size);

/** Estimate the compressibility of transposed and delta-transposed input. */
STENOS_EXPORT size_t stenos_private_assess_compressibility(const void* src, size_t bytesoftype, size_t bytes, void* buffer);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* STENOS_H */
