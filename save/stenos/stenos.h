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
	* Returns the maximum compressed size for given bytes.
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
#include "stddef.h"
#ifdef __cplusplus
}
#endif
#include "stenos_export.h"

/**
* Default block size in stenos library
*/
#define STENOS_BLOCK_SIZE (131072) // bytes
/**
* Maximum block size in stenos library
*/
#define STENOS_MAX_BLOCK_BYTES ((1u << 24u) - 1u) // 16777216
/**
* Maximum bytes of type in stenos library
*/
#define STENOS_MAX_BYTESOFTYPE (STENOS_MAX_BLOCK_BYTES / 256)

/**
* Disable custom block size with stenos_set_block_size()
*/
#define STENOS_NO_BLOCK_SHIFT ((size_t)-1)

/**
* Stenos error codes
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

typedef struct stenos_timer stenos_timer_t;

STENOS_EXPORT stenos_timer_t* stenos_make_timer();
STENOS_EXPORT void stenos_destroy_timer(stenos_timer_t* timer);
STENOS_EXPORT void stenos_tick(stenos_timer_t* timer);
STENOS_EXPORT uint64_t stenos_tock(stenos_timer_t* timer);

typedef struct stenos_context stenos_context_t;

STENOS_EXPORT stenos_context_t* stenos_make_context();
STENOS_EXPORT void stenos_destroy_context(stenos_context_t* ctx);
STENOS_EXPORT void stenos_reset_context(stenos_context_t* ctx);
STENOS_EXPORT size_t stenos_set_level(stenos_context_t* ctx, int level);
STENOS_EXPORT size_t stenos_set_threads(stenos_context_t* ctx, int threads);
STENOS_EXPORT size_t stenos_set_max_nanoseconds(stenos_context_t* ctx, uint64_t nanoseconds);

/**
* Set a custom block size. A value of STENOS_NO_BLOCK_SHIFT disable the custom block size.
* 
* The custom block size is expressed in block shift, where the actual
* block size becomes : (bytesoftype * 256) << blocksize_shift.
* 
* Custom block size should only be used in order to deserialize the 
* compressed content from a stenos::cvector. In this case, the BlockSize 
* template parameter of stenos::cvector must be equal to blocksize_shift.
*/
STENOS_EXPORT size_t stenos_set_block_size(stenos_context_t* ctx, size_t blocksize_shift);

STENOS_EXPORT int stenos_has_error(size_t r);

STENOS_EXPORT size_t stenos_bound(size_t bytes);

STENOS_EXPORT size_t stenos_compress_generic(stenos_context_t* ctx, const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);
STENOS_EXPORT size_t stenos_decompress_generic(stenos_context_t* ctx, const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);

STENOS_EXPORT size_t stenos_compress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size, int level);
STENOS_EXPORT size_t stenos_decompress(const void* src, size_t bytesoftype, size_t bytes, void* dst, size_t dst_size);


typedef struct stenos_info
{
	size_t decompressed_size;
	size_t superblock_size;
} stenos_info_t;
	
STENOS_EXPORT size_t stenos_info(const void* src, size_t bytesoftype, size_t bytes, stenos_info_t* info);


/**
* Private API
*/

STENOS_EXPORT size_t stenos_private_compress_block(stenos_context_t* ctx, const void* src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* dst, size_t dst_size);

STENOS_EXPORT size_t stenos_private_decompress_block(stenos_context_t* ctx, const void* _src, size_t bytesoftype, size_t super_block_size, size_t bytes, void* _dst, size_t dst_size);

STENOS_EXPORT size_t stenos_private_block_size(const void* _src, size_t src_size);

STENOS_EXPORT size_t stenos_private_create_compression_header(size_t decompressed_size, size_t super_block_size, void* _dst, size_t dst_size);

#ifdef __cplusplus
}
#endif

#endif