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

#ifndef STENOS_H
#define STENOS_H

#ifdef __cplusplus

namespace stenos
{
	/**
	@brief Returns the maximum compressed size for given bytes.
	Similar to stenos_bound but for C++ and constexpr. 
	*/
	inline constexpr size_t compress_bound(size_t bytes)
	{
		constexpr size_t min_superblock_size = 65792; /* Minimum superblock size, for bytesoftype 257*/
		size_t super_block_count = bytes / min_superblock_size + (bytes % min_superblock_size ? 1 : 0);
		return 12 + (super_block_count == 0 ? 1 : super_block_count) * 4 + bytes;
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

/**
Default superblock size in stenos library
*/
#define STENOS_BLOCK_SIZE (131072) // bytes
/**
Maximum superblock size in stenos library
*/
#define STENOS_MAX_BLOCK_BYTES ((1u << 24u) - 1u) // 16777216
/**
Maximum bytes of type in stenos library
*/
#define STENOS_MAX_BYTESOFTYPE (STENOS_MAX_BLOCK_BYTES / 256)

/**
Disable custom block size with stenos_set_block_size()
*/
#define STENOS_NO_BLOCK_SHIFT ((size_t)-1)

/**
Stenos error codes
*/
#define STENOS_ERROR_UNDEFINED ((size_t)(-1))
#define STENOS_ERROR_SRC_OVERFLOW ((size_t)(-2))
#define STENOS_ERROR_ALLOC ((size_t)(-3))
#define STENOS_ERROR_INVALID_INPUT ((size_t)(-4))
#define STENOS_ERROR_INVALID_INSTRUCTION_SET ((size_t)(-5))
#define STENOS_ERROR_DST_OVERFLOW ((size_t)(-6))
#define STENOS_ERROR_INVALID_BYTESOFTYPE ((size_t)(-7))
#define STENOS_ERROR_ZSTD_INTERNAL ((size_t)(-8))
#define STENOS_ERROR_INVALID_PARAMETER ((size_t)(-9))
#define STENOS_LAST_ERROR_CODE ((size_t)(-100))

#ifdef __cplusplus
extern "C" {
#endif


/********************************************
 Compression/decompression API
********************************************/



/**
@brief Compression/decompression context object.

stenos_context stores compression/decompression parameters 
as well as required buffers.

It is usually wise to reuse compression contexts as they will 
(if possible) reuse previously allocated memory.
*/
typedef struct stenos_context_s stenos_context;

/**
@brief Creates a new stenos_context object.

The context is initialized with default parameters:
	- level = 1
	- threads = 1
	- no maximum time.

Returns NULL on error.
*/
STENOS_EXPORT stenos_context* stenos_make_context();

/**
@brief Destroy a compression context.

This deallocate all memory allocated by this context.
*/
STENOS_EXPORT void stenos_destroy_context(stenos_context* ctx);

/**
@brief Reset a compression context to its initial parameters.
*/
STENOS_EXPORT void stenos_reset_context(stenos_context* ctx);

/**
@brief Set the compression level to a compression context.

The compression level ranges from 0 (copy without compression) 
to 9 (maximum compression).
*/
STENOS_EXPORT size_t stenos_set_level(stenos_context* ctx, int level);

/**
@brief Set the number of threads used for compression/decompression.
*/
STENOS_EXPORT size_t stenos_set_threads(stenos_context* ctx, int threads);

/**
@brief Set the maximum time in nanoseconds allowed for compression.
Set to 0 to disable time bounded compression.

The compression process will automatically and continually adjust 
its compression level to avoid (if possible) reaching the maximum
time. If, no matter what, the compression process is too slow
to fullfill the time limit, direct memcpy will be used to "compress"
the remaining bytes.

The time precision higly depends on the targetted platform. Typically,
on Windows, the compression process (almost) never exceed the
compression time by more than a millisecond.
*/
STENOS_EXPORT size_t stenos_set_max_nanoseconds(stenos_context* ctx, uint64_t nanoseconds);

/**
@brief Set a custom superblock size. A value of STENOS_NO_BLOCK_SHIFT disable the custom block size.

The custom superblock size is expressed in block shift, where the actual
block size becomes : (bytesoftype * 256) << blocksize_shift.

Custom superblock size should only be used in order to deserialize the 
compressed content from a stenos::cvector. In this case, the BlockSize 
template parameter of stenos::cvector must be equal to blocksize_shift.
*/
STENOS_EXPORT size_t stenos_set_block_size(stenos_context* ctx, size_t blocksize_shift);

/**
@brief Check for error the result value of stenos_compress_generic(), stenos_compress(),
stenos_decompress_generic(), stenos_decompress(), stenos_info_s(), stenos_set_level(),
stenos_set_threads(), stenos_set_max_nanoseconds() and stenos_set_block_size().
*/
STENOS_EXPORT int stenos_has_error(size_t r);

/**
@brief Returns the maximum compressed size for given input size.
*/
STENOS_EXPORT size_t stenos_bound(size_t bytes);

/**
@brief Generic compression function.
@param ctx compression context
@param src input bytes
@param bytesoftype number of bytes of a single element
@param bytes total number of input bytes
@param dst destination buffer
@param dst_size destination buffer size
@return the number of bytes compressed, or an error code.
@warning the input and output buffers cannot overlapp.
*/
STENOS_EXPORT size_t stenos_compress_generic(stenos_context* ctx, const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);

/**
@brief Generic decompression function.
@param ctx decompression context
@param src compressed input bytes
@param bytesoftype number of bytes of a single element, must be same as used in stenos_compress_generic()
@param bytes total number of input bytes
@param dst destination buffer
@param dst_size destination buffer size
@return the number of bytes decompressed, or an error code.
@warning the input and output buffers cannot overlapp.
*/
STENOS_EXPORT size_t stenos_decompress_generic(stenos_context* ctx, const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);

/**
@brief Compression function.
@param src input bytes
@param bytesoftype number of bytes of a single element
@param bytes total number of input bytes
@param dst destination buffer
@param dst_size destination buffer size
@param level the compression level between 0 (copy without compression) and 9 (maximum compression)
@return the number of bytes compressed, or an error code.
@warning the input and output buffers cannot overlapp.
*/
STENOS_EXPORT size_t stenos_compress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size, int level);

/**
@brief Decompression function.
@param ctx decompression context
@param src compressed input bytes
@param bytesoftype number of bytes of a single element, must be same as used in stenos_compress_generic()
@param bytes total number of input bytes
@param dst destination buffer
@param dst_size destination buffer size
@return the number of bytes decompressed, or an error code.
@warning the input and output buffers cannot overlapp.
*/
STENOS_EXPORT size_t stenos_decompress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);


/**
@brief Small class gathering information on a compressed frame.
*/
typedef struct stenos_info_s
{
	size_t decompressed_size;	/* Total decompressed size (bytes) */
	size_t superblock_size		/* Superblock size (bytes) */;
} stenos_info;

/**
@brief Gather some information on a compressed frame.
@param src compressed input
@param bytesoftype size of a single element. Must be the same one as used in stenos_compress() or stenos_decompress_generic().
@param bytes input size. Does not need to be the full compressed length.
@param info output information on the compressed frame.
@return the number of bytes read to get these information (at most 12 bytes) or an error code on failure.
*/
STENOS_EXPORT size_t stenos_get_info(const void* src, size_t bytesoftype, size_t bytes, stenos_info* info);






/********************************************
 Timer API
********************************************/


/**
@brief Precise timer class used to compute elapsed time in nanoseconds.
The actual precision depends on the current platform.
*/
typedef struct stenos_timer_s stenos_timer;

/**
@brief Create a precise timer object.
Returns NULL on error.
*/
STENOS_EXPORT stenos_timer* stenos_make_timer();

/**
@brief Destroy a precise timer object.
Check for NULL object.
*/
STENOS_EXPORT void stenos_destroy_timer(stenos_timer* timer);

/**
@brief Reset timer object.
*/
STENOS_EXPORT void stenos_tick(stenos_timer* timer);

/**
@brief Returns elapsed nanoseconds since the last call to stenos_tick().
*/
STENOS_EXPORT uint64_t stenos_tock(stenos_timer* timer);






/********************************************
 Private API used by stenos::cvector
********************************************/


STENOS_EXPORT size_t stenos_private_compress_block(stenos_context* ctx, const void* src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* dst, size_t dst_size);

STENOS_EXPORT size_t stenos_private_decompress_block(stenos_context* ctx, const void* _src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* _dst, size_t dst_size);

STENOS_EXPORT size_t stenos_private_block_size(const void* _src, size_t src_size);

STENOS_EXPORT size_t stenos_private_create_compression_header(size_t decompressed_size, size_t super_block_size, void* _dst, size_t dst_size);




#ifdef __cplusplus
}
#endif

#endif