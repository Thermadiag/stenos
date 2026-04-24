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

#include "stenos.h"

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
} stenosv_pixel_type;


#ifdef __cplusplus

#include "bits.hpp"
/**
For C++ users, convert arithmetic type to stenosv_pixel_type
*/
template<class T>
static STENOS_CONSTEXPR stenosv_pixel_type stenosv_to_pixel_type()
{
	static_assert(std::is_arithmetic_v<T>, "invalid type");
	if STENOS_CONSTEXPR (sizeof(T) == 1) {
		if STENOS_CONSTEXPR (std::is_signed_v<T>)
			return StenosInt8;
		else
			return StenosUInt8;
	}
	else if STENOS_CONSTEXPR (sizeof(T) == 2) {
		if STENOS_CONSTEXPR (std::is_signed_v<T>)
			return StenosInt16;
		else
			return StenosUInt16;
	}
	else if STENOS_CONSTEXPR (sizeof(T) == 4) {
		if STENOS_CONSTEXPR (std::is_integral_v<T>) {
			if STENOS_CONSTEXPR (std::is_signed_v<T>)
				return StenosInt32;
			else
				return StenosUInt32;
		}
		else
			return StenosFloat32;
	}
	else {
		if STENOS_CONSTEXPR (std::is_integral_v<T>) {
			if STENOS_CONSTEXPR (std::is_signed_v<T>)
				return StenosInt64;
			else
				return StenosUInt64;
		}
		else
			return StenosFloat64;
	}
}



extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
Video block format version
*/
#define STENOS_VIDEO_TRACE_VERSION 1

/**
Structure describing a GPU device
*/
typedef struct stenosv_gpu_device
{
	const char* name;
	uint64_t compute_units;
	uint64_t max_memory;
	uint64_t vendor_id;
} stenosv_gpu_device;

/**
Generic buffer class
*/
typedef struct stenosv_payload_s
{
	void* data;
	uint64_t size;
} stenosv_payload;

/**
Header of a compressed video block
*/
typedef struct stenosv_block_header
{
	unsigned char version;	  /* Codec version */
	unsigned char pixel_type; /* Pixel type (stenosv_pixel_type) */
	unsigned short width;	  /* Image width */
	unsigned short height;	  /* Image height */
	unsigned short count;	  /* Number of images */
} stenosv_block_header;

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
} stenosv_trace_component;

/**
 * Pixel coordinate type for trace_query structure
 */
typedef struct stenosv_coordinate
{
	unsigned x, y;
} stenosv_coordinate;

/**
 * Time trace query information as used by stenosv_extract_time_trace().
 */
typedef struct stenosv_trace_query
{
	int threads;		    /* Number of thread used for trace extraction*/
	int components;		    /*  Parameters to extract, combination of stenosv_trace_component*/
	stenosv_coordinate* pixels; /*  Region Of Intereset(ROI) pixels on which time trace is computed*/
	unsigned pixel_count;
} stenosv_trace_query;

/**
Output of stenosv_extract_time_trace().
Members must be allocated up front if they are going to be computed (based on stenosv_trace_query::components).
Note that the timestamps must ALWAYS be allocated.
*/
typedef struct stenosv_trace_result
{
	int64_t* timestamps;
	double* max_values;
	double* min_values;
	double* mean_values;
	double* var_values;
} stenosv_trace_result;




/*************************************************************************
Video compression API
*************************************************************************/

/**
Video compression context structure
*/
typedef struct stenosv_compress_s stenosv_compress;

/**
Returns the array of compatible GPU devices for video time trace compression.
count is set to the number of compatible gpu devices.
*/
STENOS_EXPORT stenosv_gpu_device* stenosv_list_gpu_devices(int* count);

/**
Returns the default GPU device for video time trace compression.

The default GPU is the one having the most compute units.
Returns -1 if no suitable device is found, or if OpenCL support is disabled.
*/
STENOS_EXPORT int stenosv_default_gpu_device();

/**
Create a video compression context for given pixel type, image size, Group Of Pictures (GOP) and device.

The device value if the GPU index in the array of compatible GPUs as returned by stenosv_list_gpu_devices().
A device value of -1 disable GPU support and will use the CPU version of the compression algorithm.

A stenosv_compress object can compress any number of Group Of Pictures (i.e. a full video if necessary).

Returns a null pointer on error.
*/
STENOS_EXPORT stenosv_compress* stenosv_compress_make(stenosv_pixel_type type, int width, int height, int GOP, int device);

/**
Destroy/deallocate a stenosv_compress object.
*/
STENOS_EXPORT void stenosv_compress_destroy(stenosv_compress*);

/**
Set the number of threads used by a video compressor (default to 1).

Even for GPU based compression, using multiple threads will still
fasten the compression process as several stage of the compression algorithm
are CPU bounded.
*/
STENOS_EXPORT void stenosv_compress_set_threads(stenosv_compress*, int threads);

/**
Set the compression level from 0 (no compression) to 9 (maximum compression)
*/
STENOS_EXPORT void stenosv_compress_set_clevel(stenosv_compress*, int level);

/**
Set the maximum error per pixel (default to 0).

This enables lossy compression with bounded error.
*/
STENOS_EXPORT void stenosv_compress_set_max_error(stenosv_compress*, double error);

/**
Add an image to the video compressor.

Input image must be of the same pixel type and dimensions as values passed to stenosv_compress_make().
The timestamp is in arbitrary unit, but must be monotonic.

Returns 0 if the compressor is waiting for more images, 1 if the compressor finished its Group Of pictures.
In this case, stenosv_compress_payload() must be called to retrieve the compressed payload, and next call
to stenosv_compress_add_image() will automatically start a new Group Of Pictures.

This function might returns and error code on failure.
*/
STENOS_EXPORT size_t stenosv_compress_add_image(stenosv_compress*, void* img, int64_t timestamp);

STENOS_EXPORT size_t stenosv_compress_add_image_bytes(stenosv_compress*, void* img, int inner_stride_bytes, int64_t timestamp);

/**
Stop the current Group Of Pictures.
stenosv_compress_payload() must then be called to retrieve the compressed payload.

After a call to this function, calling stenosv_compress_add_image() will automatically start a new Group Of Pictures.

Returns 0 on success, an error code on failure.
*/
STENOS_EXPORT size_t stenosv_compress_stop(stenosv_compress*);

/**
Returns the last compressed payload, or an empty payload if the Group Of Pictures is not finished.

This function should be called if stenosv_compress_add_image() returns 1 or after a call to stenosv_compress_stop().
*/
STENOS_EXPORT stenosv_payload stenosv_compress_payload(stenosv_compress*);

/*************************************************************************
Video decompression API
*************************************************************************/

/**
Video decompression context structure
*/
typedef struct stenosv_decompress_s stenosv_decompress;

/**
Helper function, create a stenos_input from a buffer.
*/
STENOS_EXPORT stenos_input stenosv_input_from_payload(void* data, uint64_t size);

/**
Returns the block header for given payload.

Passed buffer must point to the start of a compressed payload (or Group Of Pictures), but does not need to contain the full payload.
if opt_full_block_size is not null, it will be set to the full payload size.

Returns an invalid block header on error (stenosv_block_header::version == 0).
*/
STENOS_EXPORT stenosv_block_header stenosv_read_block_header_buffer(stenosv_payload buffer, uint64_t* opt_full_block_size);

/**
Returns the block header for given payload.

Passed buffer must point to the start of a compressed payload (or Group Of Pictures), but does not need to contain the full payload.
if opt_full_block_size is not null, it will be set to the full payload size.

Returns an invalid block header on error (stenosv_block_header::version == 0).
*/
STENOS_EXPORT stenosv_block_header stenosv_read_block_header_stream(stenos_input* input, uint64_t* opt_full_block_size);

/**
Create a video decompression context from a complete payload.

The payload must contain a full Group Of Pictures.
The returned object is only valid for reading images from this Group Of Pictures.

This function partially decompress GOP images, and will use given threads for that.
The passed threads will also be used to read images with stenosv_decompress_read_image().

Returns a null pointer on error.
*/
STENOS_EXPORT stenosv_decompress* stenosv_decompress_make_buffer(stenosv_payload buffer, int threads);

/**
Create a video decompression context from a complete payload.

The input stream must contain at least a full Group Of Pictures.
If the context creation succeeds, the input stream position is set to the end of the GOP (and potentially the beginning of next one).
The returned object is only valid for reading images from this Group Of Pictures.

This function partially decompress GOP images, and will use given threads for that.
The passed threads will also be used to read images with stenosv_decompress_read_image().

Returns a null pointer on error.
*/
STENOS_EXPORT stenosv_decompress* stenosv_decompress_make_stream(stenos_input* input, int threads);

/**
Destroy/deallocate video decompression context.
*/
STENOS_EXPORT void stenosv_decompress_destroy(stenosv_decompress*);

/**
Returns the block header of a video decompression context.
*/
STENOS_EXPORT stenosv_block_header stenosv_decompress_info(stenosv_decompress*);

/**
Returns the timestamps contained by a video decompression context.
*/
STENOS_EXPORT int64_t* stenosv_decompress_get_timestamps(stenosv_decompress*);

/**
Read an image at given position from a video decompression context.

The image must have the pixel type and dimensions of the Group Of Pictures.
Use stenosv_decompress_info() to retrieve these information.

An inner pixel stride different than 1 can be passed in order to reconstruct
multi-channel images (like RGB ones).
*/
STENOS_EXPORT size_t stenosv_decompress_read_image(stenosv_decompress*, int pos, int inner_stride, void* out_image);

/**
Read an image at given position from a video decompression context.

The image must have the pixel type and dimensions of the Group Of Pictures.
Use stenosv_decompress_info() to retrieve these information.

The inner stride is given in bytes. Unlike stenosv_decompress_read_image,
this function can reconstruct a multi-channel image where each channel
has a different size.
*/
STENOS_EXPORT size_t stenosv_decompress_read_image_bytes(stenosv_decompress*, int pos, int inner_stride_bytes, void* out_image);

/*************************************************************************
Time trace extraction API
*************************************************************************/

/**
Initialize a time trace query
*/
inline void stenosv_init_trace_query(stenosv_trace_query* q)
{
	memset(q, 0, sizeof(stenosv_trace_query));
}
/**
Initialize a time trace result
*/
inline void stenosv_init_trace_result(stenosv_trace_result* r)
{
	memset(r, 0, sizeof(stenosv_trace_result));
}

/**
Extract time trace information from a Group Of Pictures.

The input stream must point to the beginning of a compressed GOP.
Note that, in case of success, the input stream won't necessarly point to the GOP end.

The query parameter specifies:
	- Which features to extract (stenosv_trace_query::components, combination of stenosv_trace_component)
	- The number of threads to use (stenosv_trace_query::threads)
	- The Pixels to consider (stenosv_trace_query::pixels and stenosv_trace_query::pixel_count).

The out_trace object will hold extracted features. Its member arrays must point to valid memory addresses, at least for extracted components.
For instance, if query->components contains StenosTraceMax, out_trace->max_values must be valid and point to a meory area of at least the GOP size.
Note that the query->timestamps must ALWAYS be valid.

This function can be called from multiple threads using the same input stream.
For that, a valid lock object must be passed and this object must be in the locked state.

Returns the GOP size on success, an error code on failure.

*/
STENOS_EXPORT size_t stenosv_extract_time_trace(stenos_input* input, stenosv_trace_query* query, stenosv_trace_result* out_trace, stenos_lock* opt_lock);

#ifdef __cplusplus
} // end extern "C"
#endif

#endif
