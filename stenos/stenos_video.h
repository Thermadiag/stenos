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

#ifndef STENOS_VIDEO_H
#define STENOS_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
}
#endif

#include "stenos.h"


#define STENOS_VIDEO_TRACE_VERSION 1

/**
Structure describing a GPU device
*/
typedef struct stenos_gpu_device
{
	const char* name;
	uint64_t compute_units;
	uint64_t max_memory;
	uint64_t vendor_id;
} stenos_gpu_device;


/**
Supported image pixel type for video compression/decompression
*/
typedef enum
{
	StenosInt8,
	StenosUInt8,
	StenosInt16,
	StenosUInt16,
	StenosInt32,
	StenosUInt32,
	StenosInt64,
	StenosUInt64,
	StenosFloat32,
	StenosFloat64,
} stenos_pixel_type;

/**
Generic buffer class
*/
typedef struct stenos_payload_s
{
	void* data;
	uint64_t size;
} stenos_payload;

/**
Header of a compressed video block
*/
typedef struct stenos_vblock_header
{
	unsigned char version; /* Codec version */
	unsigned char pixel_type; /* Pixel type (stenos_pixel_type) */
	unsigned short width;	  /* Image width */
	unsigned short height;	/* Image height */
	unsigned short count; /* Number of images */
} stenos_vblock_header;


/**
 * Time trace features to extract using extract_time_trace().
 */
typedef enum
{
	StenosTraceMin = 1,
	StenosTraceMax = 2,
	StenosTraceMean = 4,
	StenosTraceVar = 8,
	StenosTraceAll = 15
} stenos_trace_component;

/**
 * Pixel coordinate type for trace_query structure
 */
typedef struct stenos_coordinate
{
	unsigned x, y;
} stenos_coordinate;

/**
 * Time trace query information as used by stenos_extract_time_trace().
 */
typedef struct stenos_trace_query
{
	int threads;		   /* Number of thread used for trace extraction*/
	int components;		   /*  Parameters to extract*/
	stenos_coordinate* pixels; /*  Region Of Intereset(ROI) pixels on which time trace is computed*/
	unsigned pixel_count;
} stenos_trace_query;

/**
Output of stenos_extract_time_trace().
Members must be allocated up front if they are going to be computed (based on stenos_trace_query::components).
Note that the timestamps must ALWAYS be allocated.
*/
typedef struct stenos_trace_result
{
	int64_t* timestamps;
	double* max_values;
	double* min_values;
	double* mean_values;
	double* var_values;
} stenos_trace_result;



#ifdef __cplusplus

#include <type_traits>

template<class T>
static constexpr stenos_pixel_type stenos_to_pixel_type()
{
	static_assert(std::is_arithmetic_v<T>);
	if constexpr (sizeof(T) == 1) {
		if constexpr (std::is_signed_v<T>)
			return StenosInt8;
		else
			return StenosUInt8;
	}
	else if constexpr (sizeof(T) == 2) {
		if constexpr (std::is_signed_v<T>)
			return StenosInt16;
		else
			return StenosUInt16;
	}
	else if constexpr (sizeof(T) == 4) {
		if constexpr (std::is_integral_v<T>) {
			if constexpr (std::is_signed_v<T>)
				return StenosInt32;
			else
				return StenosUInt32;
		}
		else
			return StenosFloat32;
	}
	else {
		if constexpr (std::is_integral_v<T>) {
			if constexpr (std::is_signed_v<T>)
				return StenosInt64;
			else
				return StenosUInt64;
		}
		else
			return StenosFloat64;
	}
}

#endif


/*
Video compression API
*/

typedef struct stenos_vcompress_s stenos_vcompress;


STENOS_EXPORT stenos_gpu_device* stenos_list_gpu_devices(int* count);
STENOS_EXPORT int stenos_default_gpu_device();


STENOS_EXPORT stenos_vcompress* stenos_vcompress_make(stenos_pixel_type type, int width, int height, int GOP, int device);
STENOS_EXPORT void stenos_vcompress_destroy(stenos_vcompress*);

STENOS_EXPORT void stenos_vcompress_set_threads(stenos_vcompress*, int threads);
STENOS_EXPORT void stenos_vcompress_set_clevel(stenos_vcompress*, int level);
STENOS_EXPORT void stenos_vcompress_set_max_error(stenos_vcompress*, double error);

STENOS_EXPORT size_t stenos_vcompress_add_image(stenos_vcompress*, void* img, int64_t timestamp);
STENOS_EXPORT size_t stenos_vcompress_stop(stenos_vcompress*);

STENOS_EXPORT stenos_payload stenos_vcompress_payload(stenos_vcompress*);

/*
Video decompression API
*/

typedef struct stenos_vdecompress_s stenos_vdecompress;

STENOS_EXPORT stenos_vblock_header stenos_read_vblock_header_buffer(stenos_payload buffer, uint64_t* opt_full_block_size);
STENOS_EXPORT stenos_vblock_header stenos_read_vblock_header_stream(stenos_input* input, uint64_t* opt_full_block_size);

STENOS_EXPORT stenos_vdecompress* stenos_vdecompress_make_buffer(stenos_payload buffer);
STENOS_EXPORT stenos_vdecompress* stenos_vdecompress_make_stream(stenos_input* input);
STENOS_EXPORT void stenos_vdecompress_set_threads(stenos_vdecompress*, int threads);
STENOS_EXPORT void stenos_vdecompress_destroy(stenos_vdecompress*);

STENOS_EXPORT stenos_vblock_header stenos_vdecompress_info(stenos_vdecompress*);
STENOS_EXPORT int64_t* stenos_vdecompress_get_timestamps(stenos_vdecompress*);
STENOS_EXPORT size_t stenos_vdecompress_read_image(stenos_vdecompress*, int pos, int inner_stride, void* out_image);

/**
Time trace API
*/

inline void stenos_init_trace_query(stenos_trace_query* q)
{
	memset(q, 0, sizeof(stenos_trace_query));
}
inline void stenos_init_trace_result(stenos_trace_result* r)
{
	memset(r, 0, sizeof(stenos_trace_result));
}

STENOS_EXPORT size_t stenos_extract_time_trace(stenos_input* input, stenos_trace_query* query, stenos_trace_result* out_trace, stenos_lock* opt_lock);

#endif
