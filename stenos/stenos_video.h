/**
 * @file stenos_video.h
 * @brief C and C++ API for temporal compression of arithmetic image sequences.
 *
 * Stenos Video compresses sequences of two-dimensional arithmetic images using
 * temporal prediction across a Group Of Pictures (GOP). Unlike conventional
 * colour video codecs, the codec operates directly on scalar signed integers,
 * unsigned integers, or floating-point samples.
 *
 * Both lossless and bounded-error lossy compression are supported. In lossy
 * mode, the maximum admissible reconstruction error is configured through
 * stenosv_compress_set_max_error().
 *
 * A compressed video is represented as a sequence of independent compressed
 * GOP payloads. Individual GOPs can be decoded independently and may also be
 * inspected to extract timestamps or temporal statistics without reconstructing
 * every image.
 *
 * @note Unless explicitly stated otherwise, context objects are not safe for
 * concurrent access. Distinct contexts may be used concurrently.
 *
 * @note Functions returning size_t may return either a non-negative result or
 * a Stenos error code. Use stenos_has_error() to distinguish errors.
 */

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
 * @brief Scalar sample formats supported by the video codec.
 *
 * Every image in a compressed GOP has one fixed scalar type. Multi-channel or
 * compound layouts can be handled by the stride-aware input and output APIs.
 */
typedef enum
{
	StenosInt8,    /**< Signed 8-bit integer. */
	StenosUInt8,   /**< Unsigned 8-bit integer. */
	StenosInt16,   /**< Signed 16-bit integer. */
	StenosUInt16,  /**< Unsigned 16-bit integer. */
	StenosInt32,   /**< Signed 32-bit integer. */
	StenosUInt32,  /**< Unsigned 32-bit integer. */
	StenosInt64,   /**< Signed 64-bit integer. */
	StenosUInt64,  /**< Unsigned 64-bit integer. */
	StenosFloat32, /**< IEEE-754 single-precision floating point. */
	StenosFloat64, /**< IEEE-754 double-precision floating point. */
} stenosv_pixel_type;

#ifdef __cplusplus

#include "bits.hpp"

/**
 * @brief Map a C++ arithmetic type to its corresponding Stenos pixel type.
 *
 * @tparam T Arithmetic scalar type.
 * @return The matching stenosv_pixel_type value.
 *
 * @note The mapping is based on type category, signedness, and sizeof(T).
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

/** Current compressed video block format version. */
#define STENOS_VIDEO_TRACE_VERSION 1

/**
 * @brief Description of a GPU device usable by the video codec.
 *
 * The returned device descriptions are owned by the library and must not be
 * modified or released by the caller.
 */
typedef struct stenosv_gpu_device
{
	const char* name;	/**< Human-readable device name. */
	uint64_t compute_units; /**< Number of reported compute units. */
	uint64_t max_memory;	/**< Maximum device memory in bytes. */
	uint64_t vendor_id;	/**< Vendor identifier reported by the backend. */
} stenosv_gpu_device;

/**
 * @brief Generic non-owning memory buffer.
 *
 * Ownership and lifetime depend on the function returning or consuming the
 * payload. In particular, stenosv_compress_payload() returns library-owned
 * memory whose validity is tied to the compressor context.
 */
typedef struct stenosv_payload_s
{
	void* data;    /**< Pointer to the first byte, or NULL for an empty payload. */
	uint64_t size; /**< Number of valid bytes available at data. */
} stenosv_payload;

/**
 * @brief Metadata stored at the beginning of a compressed GOP.
 *
 * A header with version equal to zero denotes an invalid or unreadable block.
 */
typedef struct stenosv_block_header
{
	unsigned char version;	  /**< Codec/block format version. */
	unsigned char pixel_type; /**< Scalar format, castable to stenosv_pixel_type. */
	unsigned short width;	  /**< Image width in pixels. */
	unsigned short height;	  /**< Image height in pixels. */
	unsigned short count;	  /**< Number of images in the GOP. */
} stenosv_block_header;

/**
 * @brief Bit flags selecting temporal statistics to extract.
 *
 * Values may be combined with the bitwise OR operator.
 */
typedef enum
{
	StenosTraceMin = 1,	/**< Minimum value in each image over the ROI. */
	StenosTraceMax = 2,	/**< Maximum value in each image over the ROI. */
	StenosTraceMean = 4,	/**< Arithmetic mean in each image over the ROI. */
	StenosTraceVar = 8,	/**< Variance in each image over the ROI. */
	StenosTraceMinPos = 16, /**< Position of the minimum value. */
	StenosTraceMaxPos = 32, /**< Position of the maximum value. */
	StenosTraceAll = 63	/**< Extract every supported component. */
} stenosv_trace_component;

/** @brief Zero-based pixel coordinate. */
typedef struct stenosv_coordinate
{
	unsigned x; /**< Horizontal coordinate in [0, width). */
	unsigned y; /**< Vertical coordinate in [0, height). */
} stenosv_coordinate;

/**
 * @brief Parameters controlling temporal trace extraction.
 *
 * The pixels array defines the Region Of Interest (ROI). All coordinates must
 * lie within the GOP image dimensions. The library reads but does not modify
 * this array.
 */
typedef struct stenosv_trace_query
{
	int threads;		    /**< Requested worker count; values below 1 are implementation-defined. */
	int components;		    /**< Bitwise OR of stenosv_trace_component values. */
	stenosv_coordinate* pixels; /**< ROI coordinates; may be NULL only when pixel_count is zero. */
	unsigned pixel_count;	    /**< Number of entries in pixels. */
} stenosv_trace_query;

/**
 * @brief Caller-provided output arrays for temporal trace extraction.
 *
 * Each enabled component requires a corresponding output array with room for
 * at least one value per selected image. The timestamps array is mandatory even
 * when no statistical component is requested.
 *
 * The structure does not own any pointed-to memory.
 */
typedef struct stenosv_trace_result
{
	int64_t* timestamps;	     /**< Mandatory output timestamps. */
	double* max_values;	     /**< Output maxima when StenosTraceMax is enabled. */
	double* min_values;	     /**< Output minima when StenosTraceMin is enabled. */
	double* mean_values;	     /**< Output means when StenosTraceMean is enabled. */
	double* var_values;	     /**< Output variances when StenosTraceVar is enabled. */
	stenosv_coordinate* min_pos; /**< Output minimum positions when StenosTraceMinPos is enabled. */
	stenosv_coordinate* max_pos; /**< Output maximum positions when StenosTraceMaxPos is enabled. */
} stenosv_trace_result;

/** @name Video compression API
 * @{ */

/** @brief Opaque temporal video compression context. */
typedef struct stenosv_compress_s stenosv_compress;

/**
 * @brief Return the storage size of one scalar sample type.
 * @param type Pixel type to inspect.
 * @return Sample size in bytes, or zero for an invalid type.
 */
STENOS_EXPORT size_t stenosv_sizeof_pixel_type(stenosv_pixel_type type);

/**
 * @brief Enumerate compatible GPU devices.
 * @param[out] count Receives the number of returned devices. Must not be NULL.
 * @return Library-owned array of device descriptors, or NULL when no compatible
 * device is available or GPU support is disabled.
 *
 * @warning The returned array and strings must not be modified or freed.
 */
STENOS_EXPORT stenosv_gpu_device* stenosv_list_gpu_devices(int* count);

/**
 * @brief Return the preferred compatible GPU device index.
 * @return Index into stenosv_list_gpu_devices(), or -1 when no suitable GPU is
 * available or GPU support is disabled.
 *
 * The preferred device is currently selected by reported compute-unit count.
 */
STENOS_EXPORT int stenosv_default_gpu_device();

/**
 * @brief Create a reusable video compression context.
 * @param type Scalar sample format of every input image.
 * @param width Image width in pixels; must be positive.
 * @param height Image height in pixels; must be positive.
 * @param GOP Maximum number of images per Group Of Pictures; must be positive.
 * @param device GPU index returned by stenosv_list_gpu_devices(), or -1 to use
 * the CPU implementation.
 * @return New context, or NULL on invalid arguments, unsupported configuration,
 * or allocation/backend failure.
 *
 * One context may encode any number of consecutive GOPs with the same geometry
 * and pixel type. Destroy it with stenosv_compress_destroy().
 */
STENOS_EXPORT stenosv_compress* stenosv_compress_make(stenosv_pixel_type type, int width, int height, int GOP, int device);

/**
 * @brief Destroy a compression context.
 * @param compressor Context to destroy; NULL is permitted if supported by the implementation.
 */
STENOS_EXPORT void stenosv_compress_destroy(stenosv_compress* compressor);

/**
 * @brief Set the number of CPU worker threads used during compression.
 * @param compressor Compression context.
 * @param threads Requested number of threads, normally at least 1.
 *
 * CPU work remains involved when a GPU backend is selected, so multiple threads
 * may still improve throughput.
 */
STENOS_EXPORT void stenosv_compress_set_threads(stenosv_compress* compressor, int threads);

/**
 * @brief Set the compression level.
 * @param compressor Compression context.
 * @param level Compression level from 0 (minimal/no compression) to 9 (maximum compression).
 */
STENOS_EXPORT void stenosv_compress_set_clevel(stenosv_compress* compressor, int level);

/**
 * @brief Configure the maximum permitted reconstruction error per scalar sample.
 * @param compressor Compression context.
 * @param error Non-negative absolute error bound. Zero requests lossless coding.
 *
 * In bounded-error mode, each reconstructed scalar sample is intended to differ
 * from its source value by no more than this value, subject to the semantics and
 * representable range of the selected pixel type.
 */
STENOS_EXPORT void stenosv_compress_set_max_error(stenosv_compress* compressor, double error);

/** @brief Return the compressor pixel type. */
STENOS_EXPORT stenosv_pixel_type stenosv_compress_pixel_type(stenosv_compress* compressor);
/** @brief Return the configured image width in pixels. */
STENOS_EXPORT int stenosv_compress_width(stenosv_compress* compressor);
/** @brief Return the configured image height in pixels. */
STENOS_EXPORT int stenosv_compress_height(stenosv_compress* compressor);
/** @brief Return the current compression level. */
STENOS_EXPORT int stenosv_compress_clevel(stenosv_compress* compressor);
/** @brief Return the configured maximum reconstruction error. */
STENOS_EXPORT double stenosv_compress_error(stenosv_compress* compressor);
/** @brief Return the configured maximum GOP length. */
STENOS_EXPORT int stenosv_compress_gop(stenosv_compress* compressor);
/** @brief Return the selected device index, or -1 for CPU operation. */
STENOS_EXPORT int stenosv_compress_device(stenosv_compress* compressor);
/** @brief Return the configured worker-thread count. */
STENOS_EXPORT int stenosv_compress_threads(stenosv_compress* compressor);
/** @brief Return the configured maximum compression time, in implementation-defined units. */
STENOS_EXPORT uint64_t stenosv_compress_max_time(stenosv_compress* compressor);

/**
 * @brief Append one tightly packed image to the current GOP.
 * @param compressor Compression context.
 * @param img Pointer to width * height scalar samples of the configured type.
 * @param timestamp Application-defined timestamp. Timestamps must be monotonic
 * within the encoded sequence and use one consistent unit.
 * @return 0 when more images are required, 1 when a GOP has just been completed,
 * or a Stenos error code.
 *
 * After a return value of 1, retrieve the encoded GOP with
 * stenosv_compress_payload(). The following call starts a new GOP automatically.
 *
 * @warning The input image is consumed during the call and need not remain valid
 * afterwards.
 */
STENOS_EXPORT size_t stenosv_compress_add_image(stenosv_compress* compressor, void* img, int64_t timestamp);

/**
 * @brief Append one strided image to the current GOP.
 * @param compressor Compression context.
 * @param img Pointer to the first scalar sample.
 * @param inner_stride_bytes Distance in bytes between two consecutive scalar
 * samples belonging to the encoded channel/field. Use sizeof(pixel_type) for a
 * tightly packed scalar image.
 * @param timestamp Application-defined monotonic timestamp.
 * @return 0 when more images are required, 1 when the GOP is complete, or a
 * Stenos error code.
 *
 * This variant is useful for interleaved or compound pixel structures.
 */
STENOS_EXPORT size_t stenosv_compress_add_image_bytes(stenosv_compress* compressor, void* img, int inner_stride_bytes, int64_t timestamp);

/**
 * @brief Finalize the current partially filled GOP.
 * @param compressor Compression context.
 * @return 0 on success, or a Stenos error code.
 *
 * Retrieve the finalized payload with stenosv_compress_payload(). A subsequent
 * image begins a new GOP automatically.
 */
STENOS_EXPORT size_t stenosv_compress_stop(stenosv_compress* compressor);

/**
 * @brief Return the most recently finalized compressed GOP.
 * @param compressor Compression context.
 * @return Library-owned payload, or {NULL, 0} when no finalized GOP is available.
 *
 * The returned memory must not be freed or modified. Its lifetime is tied to the
 * compressor and it may be invalidated by subsequent compression operations or
 * by destruction of the context.
 */
STENOS_EXPORT stenosv_payload stenosv_compress_payload(stenosv_compress* compressor);

/** @} */

/** @name Video decompression API
 * @{ */

/** @brief Opaque decoder for one compressed GOP. */
typedef struct stenosv_decompress_s stenosv_decompress;

/**
 * @brief Read GOP metadata from a memory payload.
 * @param buffer Buffer beginning at the first byte of a compressed GOP. It may
 * contain only the header rather than the complete GOP.
 * @param[out] opt_full_block_size Optional destination for the complete encoded
 * GOP size in bytes.
 * @return Parsed header, or an invalid header with version == 0 on failure.
 */
STENOS_EXPORT stenosv_block_header stenosv_read_block_header_buffer(stenosv_payload buffer, uint64_t* opt_full_block_size);

/**
 * @brief Read GOP metadata from a raw pointer and size.
 * @param data Pointer to the first byte of a compressed GOP.
 * @param size Number of accessible bytes at data.
 * @param[out] full_block_size Optional destination for the complete GOP size.
 * @return Parsed header, or an invalid header with version == 0 on failure.
 */
STENOS_EXPORT stenosv_block_header stenosv_read_block_header_buffer2(void* data, uint64_t size, uint64_t* full_block_size);

/**
 * @brief Read GOP metadata from an input stream.
 * @param input Stream positioned at the first byte of a compressed GOP.
 * @param[out] opt_full_block_size Optional destination for the complete GOP size.
 * @return Parsed header, or an invalid header with version == 0 on failure.
 *
 * @note The resulting stream position is the staring one.
 */
STENOS_EXPORT stenosv_block_header stenosv_read_block_header_stream(stenos_input* input, uint64_t* opt_full_block_size);

/**
 * @brief Create a decoder for one complete in-memory GOP.
 * @param buffer Complete compressed GOP. The memory must remain valid for the
 * lifetime of the decoder unless the implementation copies it internally.
 * @param threads Number of worker threads used for initialization and image reads.
 * @return New decoder, or NULL on malformed data, invalid arguments, or allocation failure.
 */
STENOS_EXPORT stenosv_decompress* stenosv_decompress_make_buffer(stenosv_payload buffer, int threads);

/**
 * @brief Create a decoder from the next GOP in a stream.
 * @param input Stream positioned at the beginning of a complete compressed GOP.
 * @param threads Number of worker threads used for initialization and image reads.
 * @return New decoder, or NULL on malformed data, invalid I/O, or allocation failure.
 *
 * On success, the stream is advanced to the first byte following the GOP.
 */
STENOS_EXPORT stenosv_decompress* stenosv_decompress_make_stream(stenos_input* input, int threads);

/**
 * @brief Extract timestamps from the next compressed GOP without decoding images.
 * @param input Stream positioned at the beginning of a GOP.
 * @param[out] timestamps Destination array.
 * @param count Capacity of timestamps, expressed as a number of int64_t entries.
 * @return Number of timestamps written, or a Stenos error code.
 *
 * On success, the stream is positioned immediately after the GOP.
 */
STENOS_EXPORT size_t stenosv_extract_timestamps(stenos_input* input, int64_t* timestamps, size_t count);

/** @brief Destroy a GOP decoder. */
STENOS_EXPORT void stenosv_decompress_destroy(stenosv_decompress* decompressor);

/**
 * @brief Return metadata for the GOP associated with a decoder.
 * @return The decoder block header, or an invalid header for an invalid context.
 */
STENOS_EXPORT stenosv_block_header stenosv_decompress_info(stenosv_decompress* decompressor);

/**
 * @brief Return the GOP timestamp array.
 * @return Library-owned array containing stenosv_decompress_info().count entries.
 *
 * The pointer remains owned by the decoder and becomes invalid when it is destroyed.
 */
STENOS_EXPORT int64_t* stenosv_decompress_get_timestamps(stenosv_decompress* decompressor);

/**
 * @brief Decode one image using a stride expressed in scalar elements.
 * @param decompressor GOP decoder.
 * @param pos Zero-based image index in [0, header.count).
 * @param inner_stride Distance, in scalar elements, between consecutive decoded
 * samples. Use 1 for tightly packed output.
 * @param[out] out_image Destination image buffer.
 * @return Number of bytes written, or a Stenos error code.
 */
STENOS_EXPORT size_t stenosv_decompress_read_image(stenosv_decompress* decompressor, uint64_t pos, int inner_stride, void* out_image);

/**
 * @brief Decode one image using a byte stride.
 * @param decompressor GOP decoder.
 * @param pos Zero-based image index in [0, header.count).
 * @param inner_stride_bytes Distance in bytes between consecutive scalar outputs.
 * @param[out] out_image Destination image buffer.
 * @return Number of bytes written, or a Stenos error code.
 *
 * This variant supports interleaved channels and compound pixel structures whose
 * fields may have different sizes.
 */
STENOS_EXPORT size_t stenosv_decompress_read_image_bytes(stenosv_decompress* decompressor, uint64_t pos, int inner_stride_bytes, void* out_image);

/** @} */

/** @name Bytestream API
 * @{ */

/**
 * @brief Opaque random-access view over concatenated compressed GOPs.
 *
 * A bytestream is a complete compressed video represented by consecutive GOP
 * payloads produced by stenosv_compress.
 */
typedef struct stenosv_bytestream_s stenosv_bytestream;

/**
 * @brief Index all consecutive valid GOPs from an input stream.
 * @param input Input stream value positioned at the beginning of a bytestream.
 * @return New bytestream object, or NULL on invalid input or allocation failure.
 *
 * Scanning stops at end of stream or at the first invalid block. The stream and
 * its opaque object must remain usable for subsequent random-access reads.
 */
stenosv_bytestream* stenosv_bytestream_open(stenos_input input);

/** @brief Destroy a bytestream object. */
void stenosv_bytestream_destroy(stenosv_bytestream* stream);

/** @brief Return the common image width in pixels. */
int stenosv_bytestream_width(stenosv_bytestream* stream);
/** @brief Return the common image height in pixels. */
int stenosv_bytestream_height(stenosv_bytestream* stream);
/** @brief Return the current decoder worker count. */
int stenosv_bytestream_threads(stenosv_bytestream* stream);
/** @brief Return the common scalar sample format. */
stenosv_pixel_type stenosv_bytestream_pixel_type(stenosv_bytestream* stream);
/** @brief Return the total number of images across all indexed GOPs. */
size_t stenosv_bytestream_count(stenosv_bytestream* stream);

/**
 * @brief Return all image timestamps in display order.
 * @return Library-owned array containing stenosv_bytestream_count() entries.
 */
int64_t* stenosv_bytestream_times(stenosv_bytestream* stream);

/** @brief Return the indexed compressed bytestream size in bytes. */
uint64_t stenosv_bytestream_bytes(stenosv_bytestream* stream);

/**
 * @brief Set the number of threads used for image decoding.
 * @param stream Bytestream object.
 * @param threads Requested worker count, normally at least 1.
 */
void stenosv_bytestream_set_threads(stenosv_bytestream* stream, int threads);

/**
 * @brief Decode one tightly packed image by global image index.
 * @param stream Bytestream object.
 * @param pos Zero-based index in [0, stenosv_bytestream_count()).
 * @param[out] img Destination buffer for width * height scalar samples.
 * @return Number of bytes written, or a Stenos error code.
 */
size_t stenosv_bytestream_read(stenosv_bytestream* stream, uint64_t pos, void* img);

/** @} */

/** @name Temporal trace extraction API
 * @{ */

/**
 * @brief Initialize a trace query to all-zero defaults.
 * @param q Query to initialize; must not be NULL.
 */
inline void stenosv_init_trace_query(stenosv_trace_query* q)
{
	memset(q, 0, sizeof(stenosv_trace_query));
}

/**
 * @brief Initialize a trace result to NULL output pointers.
 * @param r Result structure to initialize; must not be NULL.
 */
inline void stenosv_init_trace_result(stenosv_trace_result* r)
{
	memset(r, 0, sizeof(stenosv_trace_result));
}

/**
 * @brief Extract timestamps and temporal statistics from one compressed GOP.
 * @param input Stream positioned at the beginning of a GOP.
 * @param query Extraction settings and ROI coordinates.
 * @param[out] out_trace Caller-allocated output arrays.
 * @param opt_lock Optional synchronization callbacks used when several callers
 * share the same underlying input stream.
 * @return Number of images processed, or a Stenos error code.
 *
 * For every enabled query component, its corresponding out_trace pointer must
 * reference an array with capacity for at least the GOP image count. The
 * timestamps pointer is always mandatory.
 *
 * The final stream position is not guaranteed to be the end of the GOP.
 * When sharing one stream between threads, the caller is responsible for
 * coordinating seek/read/tell operations through opt_lock according to the
 * implementation's locking contract.
 */
STENOS_EXPORT size_t stenosv_extract_time_trace(stenos_input* input, stenosv_trace_query* query, stenosv_trace_result* out_trace, stenos_lock* opt_lock);

/**
 * @brief Extract temporal statistics over a timestamp interval of a bytestream.
 * @param input Indexed bytestream.
 * @param start_time Requested inclusive start timestamp; may be null.
 * @param end_time Requested inclusive end timestamp; may be null.
 * processed image timestamp according to implementation semantics.
 * @param query Extraction settings and ROI coordinates.
 * @param[out] out_trace Caller-allocated output arrays.
 * @return Number of images processed, or a Stenos error code.
 *
 * Output arrays must have sufficient capacity for every image selected by the
 * requested time interval.
 */
STENOS_EXPORT size_t stenosv_bytestream_extract_time_trace(stenosv_bytestream* input, int64_t* start_time, int64_t* end_time, stenosv_trace_query* query, stenosv_trace_result* out_trace);

/** @} */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* STENOS_VIDEO_H */
