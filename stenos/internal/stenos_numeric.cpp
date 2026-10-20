/*
 * MIT License
 *
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

// Exact <=32-bit integer mode requires this matching decoder. Record layout
// is unchanged (no format version flag); legacy floating-point decoders do not
// provide this guarantee. Half-integer ties now round toward the larger value.
// No validation pass, candidate buffer, or 128-bit arithmetic is used.

#include "../stenos.h"
#include "../bits.hpp"
#include "context.h"

#include <cstdint>
#include <cstddef>
#include <limits>
#include <algorithm>
#include <vector>
#include <cmath>
#include <type_traits>

namespace stenos
{

	// Find unsigned integer type of given size
	template<size_t Bytes>
	struct MakeUnsigned
	{
		using type = std::uint8_t;
	};
	template<>
	struct MakeUnsigned<2>
	{
		using type = std::uint16_t;
	};
	template<>
	struct MakeUnsigned<4>
	{
		using type = std::uint32_t;
	};
	template<>
	struct MakeUnsigned<8>
	{
		using type = std::uint64_t;
	};

	// Gather a numeric value and an index.
	// The index is an offset to previous value.
	template<class T>
	struct ValueIndex
	{
		using index_type = typename MakeUnsigned<sizeof(T)>::type;
		T value = {};
		index_type index = 0;
	};

	

	template<class T>
	struct MakeDouble
	{
		using type = std::conditional_t<sizeof(T) == 8 && std::is_integral_v<T> && (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits), long double, double>;
	};

	// Exact slopes for integer samples up to 32 bits. Denominators are positive.
	// Numerators have magnitude < 2^33; the exact path limits distances to
	// < 2^29, so every cross-product fits in signed 64-bit arithmetic.
	struct NumericIntegerSlope
	{
		int64_t numerator = 0;
		int64_t denominator = 1;
	};
	static STENOS_ALWAYS_INLINE bool numeric_slope_less(
		NumericIntegerSlope a, NumericIntegerSlope b) noexcept
	{
		return a.numerator * b.denominator < b.numerator * a.denominator;
	}

	template<class T>
	struct NumericInterpolateSmallInteger
	{
		static_assert(std::is_integral_v<T> && sizeof(T) <= 4);

		int64_t value;
		int64_t step;
		uint64_t remainder;
		uint64_t accumulator;
		uint64_t length;

		// Preconditions: 1 <= len <= UINT32_MAX.
		NumericInterpolateSmallInteger(T a, T b, uint64_t len) noexcept
		  : value(static_cast<int64_t>(a))
		  , length(len)
		{
			const int64_t delta = static_cast<int64_t>(b) - static_cast<int64_t>(a);
			const int64_t divisor = static_cast<int64_t>(len);

			step = delta / divisor;
			int64_t rem = delta % divisor;

			// Convert truncating division to floor division.
			if (rem < 0) {
				--step;
				rem += divisor;
			}

			remainder = static_cast<uint64_t>(rem);

			// Bias for nearest-integer rounding, with ties toward +infinity.
			accumulator = len / 2;
		}

		// Returns the next sample: first call gives k = 1.
		// Call at most 'length' times.
		STENOS_ALWAYS_INLINE T next() noexcept
		{
			value += step;
			accumulator += remainder;

			if (accumulator >= length) {
				accumulator -= length;
				++value;
			}

			return static_cast<T>(value);
		}
	};

	// Exact interpolation for <=32-bit integer endpoints and <=32-bit gaps.
	// Round to nearest, with ties toward the larger value. The product fits
	// uint64_t even for the full uint32_t range and maximum uint32_t gap.
	template<class T>
	static STENOS_ALWAYS_INLINE T numeric_interpolate_small_integer(
		T a, T b, uint64_t k, uint64_t length) noexcept
	{
		static_assert(std::is_integral_v<T> && sizeof(T) <= 4);
		const int64_t left = static_cast<int64_t>(a);
		const int64_t right = static_cast<int64_t>(b);
		const int64_t lo = std::min(left, right);
		const uint64_t delta = static_cast<uint64_t>(std::max(left, right) - lo);
		const uint64_t position = left <= right ? k : length - k;
		const uint64_t product = delta * position;
		uint64_t quotient = product / length;
		const uint64_t remainder = product % length;
		if (remainder >= length - remainder) ++quotient;
		return static_cast<T>(lo + static_cast<int64_t>(quotient));
	}

	// Decimation class based on a maximum error.
	template<class T>
	struct Decimate
	{
		using VType = ValueIndex<T>;
		using Index = typename ValueIndex<T>::index_type;
		// Best effort to handle 64 bits integer without precision loss: use long double (plateform/compiler dependant)
		using Double = typename MakeDouble<T>::type;

		VType* points = nullptr;
		range<VType> last_pixels;
		std::int64_t candidate = -1;
		std::int64_t pos = 1;
		const VType* last_pos = nullptr;
		const VType* start_pos = nullptr;

		NumericIntegerSlope integer_lower{}, integer_upper{};
		bool integer_bounds_valid = false;
		int64_t integer_error = 0;

		Double max_slope = -std::numeric_limits<Double>::infinity();
		Double min_slope = std::numeric_limits<Double>::infinity();

		STENOS_ALWAYS_INLINE void clear_slopes() noexcept
		{
			integer_bounds_valid = false;
			max_slope = -std::numeric_limits<Double>::infinity();
			min_slope = std::numeric_limits<Double>::infinity();
		}

		// Compute the slope between l and r
		static STENOS_ALWAYS_INLINE Double slope(Double l, Double r, Double dist) noexcept { return (r - l) / dist; }

		// Fast absolute value
		static STENOS_ALWAYS_INLINE Double dabs(Double v) noexcept { return v < 0 ? -v : v; }

		// Returns the index of given point from the input array start
		STENOS_ALWAYS_INLINE size_t index(const VType& v) { return (size_t)(&v - start_pos); }

		// Add last valid candidate to output points
		STENOS_ALWAYS_INLINE void update_last_pos()
		{
			*points++ = VType{ last_pixels[candidate].value, (Index)(&last_pixels[candidate] - last_pos) };
			last_pos = &last_pixels[candidate];
		}

		// Same candidate/restart policy as the floating path, with exact bounds.
		STENOS_ALWAYS_INLINE void advance_integer() noexcept
		{
			const auto* val = &last_pixels[pos];
			constexpr int64_t safe_distance = int64_t{1} << 29;
			const int64_t limit = std::min<int64_t>(
				static_cast<int64_t>(std::numeric_limits<Index>::max()), safe_distance);
			bool restart = pos >= limit;
			if (!restart) {
				const NumericIntegerSlope slope{
					static_cast<int64_t>(val->value) - static_cast<int64_t>(last_pixels[0].value), pos};
				if (pos == 1 || !integer_bounds_valid) candidate = pos;
				else if (numeric_slope_less(integer_upper, slope) || numeric_slope_less(slope, integer_lower))
					restart = numeric_slope_less(integer_upper, integer_lower);
				else candidate = pos;
			}
			if (restart) {
				// A positive candidate exists: the adjacent sample always qualifies.
				update_last_pos();
				last_pixels.erase(last_pixels.begin() + candidate);
				val = &last_pixels[1];
				pos = 0; // Caller increments to 1 before processing the new anchor's successor.
				candidate = -1;
				clear_slopes();
			}
			const int64_t distance = val - &last_pixels[0];
			const int64_t delta = static_cast<int64_t>(val->value)
				- static_cast<int64_t>(last_pixels[0].value);
			const NumericIntegerSlope lower{delta - integer_error, distance};
			const NumericIntegerSlope upper{delta + integer_error, distance};
			if (!integer_bounds_valid) {
				integer_lower = lower;
				integer_upper = upper;
				integer_bounds_valid = true;
			} else {
				if (numeric_slope_less(integer_lower, lower)) integer_lower = lower;
				if (numeric_slope_less(upper, integer_upper)) integer_upper = upper;
			}
		}

		// Process last added point
		STENOS_ALWAYS_INLINE void advance_min_max(double err)
		{
			if constexpr (std::is_integral_v<T> && sizeof(T) <= 4) {
				advance_integer();
				return;
			}
			const auto* val = &last_pixels[pos];
			const auto& first = last_pixels[0];

			size_t diff_t;
			Double s;

			// We need to take into account the index type maximum size
			if STENOS_UNLIKELY (pos == std::numeric_limits<Index>::max())
				goto stop_here;

			// Compute slope
			diff_t = (size_t)(val - &first);
			s = slope((Double)first.value, (Double)val->value, (Double)diff_t);

			if (pos == 1) {
				// This is the first point, always valid
				candidate = pos;
			}
			else {
				// Get the minimum upper slope and maximum lower slope
				if ((min_slope < s) || (s < max_slope)) {

					// This point is outside: we cannot discard it.
					// However, next point(s) might bring back the slope
					// into the valid range, so we can keep investigating,
					// but without updating the last valid candidate.

					if (min_slope < max_slope) {
						// Ok, we know that next point won't be able to bring back
						// the slope to the valid range, add the last valid candidate to output
						// and restart from there.

					stop_here:
						if (candidate == -1)
							candidate = pos;

						update_last_pos();
						last_pixels.erase(last_pixels.begin() + candidate);
						val = &last_pixels[1];
						pos = 0;
						candidate = -1;
						clear_slopes();
					}
				}
				else
					candidate = pos;
			}

			// Compute upper and lower slope based on maximum error,
			// and add them to their corresponding heap structures.
			diff_t = (size_t)(val - &last_pixels[0]);
			Double upper_slope, lower_slope;
			if (diff_t == 1) {
				upper_slope = ((Double)val->value + (Double)err - (Double)last_pixels[0].value);
				lower_slope = ((Double)val->value - (Double)err - (Double)last_pixels[0].value);
			}
			else {
				upper_slope = slope((Double)last_pixels[0].value, (Double)val->value + err, (Double)diff_t);
				lower_slope = slope((Double)last_pixels[0].value, (Double)val->value - err, (Double)diff_t);
			}
			min_slope = std::min(min_slope, upper_slope);
			max_slope = std::max(max_slope, lower_slope);
		}
	};

	// Decimate input array with given maximum error.
	// With the matching decoder, integer types up to 32 bits satisfy the
	// requested absolute error (negative errors are treated as zero).
	// Floating-point and 64-bit integer types retain best-effort accuracy.
	// The decimation only drops points from input array,
	// and never create non existing points.
	template<class T>
	size_t decimate_raw_internal(const T* in, size_t count, size_t stride, ValueIndex<T>* out, double err)
	{
		using VType = ValueIndex<T>;

		if (!std::isfinite(err) || stride == 0) return STENOS_ERROR_INVALID_PARAMETER;
		if (count == 0)
			return 0;
		if (err < 0)
			err = 0;

		if constexpr (std::is_integral_v<T>)
			err = std::floor(err);

		Decimate<T> d;
		if constexpr (std::is_integral_v<T> && sizeof(T) <= 4) {
			// Capping at the type's total range cannot tighten the requested bound.
			const int64_t range = static_cast<int64_t>(std::numeric_limits<T>::max())
				- static_cast<int64_t>(std::numeric_limits<T>::lowest());
			d.integer_error = static_cast<int64_t>(std::min(err, static_cast<double>(range)));
		}
		d.candidate = -1;
		d.pos = 1;
		d.points = out;
		d.last_pos = out;
		d.start_pos = out;
		*d.points++ = { *in, 0 };
		d.last_pixels.reset(out);
		d.last_pixels.push_back({ *in, 0 });

		in += stride;
		for (size_t i = 1; i < count; ++i, ++d.pos, in += stride) {
			d.last_pixels.push_back({ *in, 0 });
			d.advance_min_max(err);
		}

		// Finish

		// Commit the last valid endpoint and restart after the new anchor.
		if (d.pos >= (int64_t)(d.last_pixels.size()) - 1 && d.candidate != -1) {
			d.update_last_pos();
			d.last_pixels.erase(d.last_pixels.begin() + d.candidate);

			d.pos = 1; // Index 0 is the anchor, already emitted.
			d.candidate = -1;
			d.clear_slopes();
		}

		for (; d.pos < static_cast<std::int64_t>(d.last_pixels.size()); ++d.pos) {
			d.advance_min_max(err);

			// At the end of the pending range, commit the last valid endpoint.
			if (d.pos == (int64_t)(d.last_pixels.size()) - 1 && d.candidate != -1) {
				d.update_last_pos();
				d.last_pixels.erase(d.last_pixels.begin() + d.candidate);

				// The loop increment advances this to 1, past the new anchor.
				d.pos = 0;
				d.candidate = -1;
				d.clear_slopes();
			}
		}

		return (size_t)(d.points - out);
	}

	template<class T>
	size_t decimate_raw(const T* in, size_t count, size_t stride, ValueIndex<T>* out, double err)
	{
		using VType = ValueIndex<T>;

		if (!std::isfinite(err) || stride == 0) return STENOS_ERROR_INVALID_PARAMETER;
		if constexpr (std::is_integral_v<T>)
			return decimate_raw_internal(in, count, stride, out, err);
		else {

			// Decimate independantly runs of finite and non finite values

			auto* dst = out;
			size_t start = 0;
			size_t i = 0;
			for (; i < count; ++i) {
				if (!is_finite(in[i * stride])) {
					if (i != start) {
						// Decimate previous finite range
						size_t written = decimate_raw_internal(in + start * stride, (i - start), stride, out, err);

						if (written != 0) {
							// Only the first output has index of 0.
							// Next finite ranges must start with an index of 1.
							out[0].index = (out == dst) ? 0 : 1;
							out += written;
						}
					}
					// output non finite value
					using Index = typename VType::index_type;
					const Index offset = (out == dst) ? Index{ 0 } : Index{ 1 };
					*out++ = VType{ in[i * stride], offset };
					start = i + 1;
				}
			}
			if (i != start) {
				// Decimate last finite range
				size_t written = decimate_raw_internal(in + start * stride, i - start, stride, out, err);

				if (written != 0) {
					out[0].index = (out == dst) ? 0 : 1;
					out += written;
				}
			}
			return (size_t)(out - dst);
		}
	}


	template<class T, class Double>
	static STENOS_ALWAYS_INLINE T cast_interpolate(Double v) noexcept
	{
		if constexpr (std::is_integral_v<T>) {
			if constexpr (sizeof(T) <= 4 && std::is_unsigned_v<T>) {
				// Double is at least double precision. For unsigned types up to
				// 32 bits, adding 0.5 is safe for values interpolated within T's range.
				return static_cast<T>(v + (Double)(0.5));
			}
			else {
				const Double rounded = std::round(v);
				const Double lo = static_cast<Double>(std::numeric_limits<T>::lowest());
				const Double hi = static_cast<Double>(std::numeric_limits<T>::max());

				if (rounded <= lo)
					return std::numeric_limits<T>::lowest();
				if (rounded >= hi)
					return std::numeric_limits<T>::max();

				return static_cast<T>(rounded);
			}
		}
		else {
			return static_cast<T>(v);
		}
	}

	// Reconstruct array previouslt decimated with decimate_raw().
	template<class T>
	size_t undecimate_raw(const ValueIndex<T>* src, size_t count, T* out, size_t dst_size, size_t stride)
	{
		using index_type = typename ValueIndex<T>::index_type;
		using Double = typename MakeDouble<T>::type;

		// At worst we have one input sample per value
		if (dst_size < count)
			return STENOS_ERROR_DST_OVERFLOW;

		T* dst = out;

		for (size_t i = 1; i < count; ++i) {
			// write first value
			if STENOS_UNLIKELY (dst_size == 0)
				return STENOS_ERROR_DST_OVERFLOW;

			*out = src[i - 1].value;
			out += stride;
			--dst_size;

			if STENOS_UNLIKELY (src[i].index == 0)
				return STENOS_ERROR_INVALID_INPUT;

			if (src[i].index > 1) {

				if STENOS_UNLIKELY ((src[i].index - 1) > dst_size)
					return STENOS_ERROR_DST_OVERFLOW;

				if constexpr (std::is_integral_v<T> && sizeof(T) <= 4) {
					const uint64_t length = src[i].index;
					
					NumericInterpolateSmallInteger<T> interpolate(src[i - 1].value, src[i].value, length);
					for (uint64_t k = 1; k < length; ++k) {
						*out = interpolate.next();
						out += stride;
					}

					dst_size -= static_cast<size_t>(length - 1);
					continue;
				}

				// Interpolate
				Double factor = (Double)1. / (Double)(src[i].index);
				Double ival = (Double)src[i].value;
				Double pval = (Double)src[i - 1].value;
				index_type start = 1;
				index_type end = src[i].index;

				while (start < end) {
					Double val1 = (Double)(start)*factor;
					val1 = ival * val1 + (1. - val1) * pval;
					*out = cast_interpolate<T>(val1);
					out += stride;
					++start;
				}
				dst_size -= (src[i].index - 1);
			}
		}
		if (count) {
			if STENOS_UNLIKELY (dst_size == 0)
				return STENOS_ERROR_DST_OVERFLOW;
			*out = src[count - 1].value;
			out += stride;
		}
		return (size_t)(out - dst) / stride;
	}

	/// @brief Helper function, returns the superblock size for given block size (bytesoftype * 256)
	static STENOS_ALWAYS_INLINE std::size_t internal_super_block_size(std::size_t block_size) noexcept
	{
		if (block_size > STENOS_BLOCK_SIZE)
			return block_size;
		return (STENOS_BLOCK_SIZE / block_size) * block_size;
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

	// Decimate and compress input array based on context maximum error.
	template<class T>
	size_t stenos_compress_numeric_type(stenos_context* ctx, const T* src, size_t count, void* dst, size_t dst_bytes)
	{
		using pixel_type = ValueIndex<T>;

		// Custom header: 1 byte for pixel type, 7 bytes for number of samples
		if STENOS_UNLIKELY (dst_bytes < 8)
			return STENOS_ERROR_DST_OVERFLOW;

		uint8_t* out = (uint8_t*)dst;
		*out++ = (uint8_t)to_numeric_type<T>();
		write_uint64_7(out, count);
		out += 7;
		dst_bytes -= 8;

		if (count == 0)
			return 8;

		int level = stenos_get_level(ctx);
		double error = stenos_get_maximum_error(ctx);
		size_t stride = stenos_get_stride(ctx);

		if STENOS_UNLIKELY (stride == 0)
			return STENOS_ERROR_INVALID_PARAMETER;

		if STENOS_UNLIKELY (!is_finite(error))
			return STENOS_ERROR_INVALID_PARAMETER;

		// Step size
		size_t step_size = 1ull << (17ull + (size_t)level / 2ull); // 130k minimum
		step_size = std::min(step_size, count);

		// Write level, used to get step_size at decompression
		if (dst_bytes == 0)
			return STENOS_ERROR_DST_OVERFLOW;
		*out++ = (uint8_t)level;
		--dst_bytes;

		// Buffer
		std::vector<pixel_type> buf(step_size);
		std::vector<uint8_t> assess_buffer(step_size * sizeof(pixel_type) * 2);

		while (count) {
			size_t rem = std::min(count, step_size);
			size_t ret = decimate_raw(src, rem, stride, buf.data(), error);
			if (stenos_has_error(ret)) return ret;
			size_t r = 0;

			if (level > 1) {

				// assess compressibility
				size_t assess_dec = stenos_private_assess_compressibility(buf.data(), sizeof(pixel_type), ret * sizeof(pixel_type), assess_buffer.data());
				size_t assess_raw = stenos_private_assess_compressibility(src, sizeof(T), rem * sizeof(T), assess_buffer.data());

				double factor = 1.3; // 1. + 0.1 / level;
				if (assess_dec * factor < assess_raw) {
					// Compress decimated values
					r = stenos_compress_generic(ctx, buf.data(), sizeof(pixel_type), ret * sizeof(pixel_type), out, dst_bytes);
					if (stenos_has_error(r))
						return r;
				}
				else {
					// Compress raw values
					buf[0] = { *src, 0 };
					for (size_t i = 1; i < rem; ++i)
						buf[i] = { src[i * stride], 1 };
					r = stenos_compress_generic(ctx, buf.data(), sizeof(pixel_type), rem * sizeof(pixel_type), out, dst_bytes);
					if (stenos_has_error(r))
						return r;
				}
			}
			else {
				// Compress (well, memcpy...) decimated values
				r = stenos_compress_generic(ctx, buf.data(), sizeof(pixel_type), ret * sizeof(pixel_type), out, dst_bytes);
				if (stenos_has_error(r))
					return r;
			}

			out += r;
			dst_bytes -= r;
			src += rem * stride;
			count -= rem;
		}

		return (size_t)(out - (uint8_t*)dst);
	}

	// Decompress an recreate numeric array previously compressed with stenos_compress_numeric_type().
	template<class T>
	size_t stenos_decompress_numeric_type(stenos_context* ctx, const void* src, size_t bytes, T* dst, size_t dst_count)
	{

		// using index_type = typename MakeUnsigned<sizeof(T)>::type;
		using pixel_type = ValueIndex<T>;

		if STENOS_UNLIKELY (bytes < 8)
			return STENOS_ERROR_SRC_OVERFLOW;

		const uint8_t* in = (const uint8_t*)src;
		if STENOS_UNLIKELY (*in != (uint8_t)to_numeric_type<T>())
			return STENOS_ERROR_INVALID_PARAMETER;
		++in;
		size_t elements = read_uint64_7(in);
		in += 7;
		bytes -= 8;

		if STENOS_UNLIKELY (elements > dst_count)
			return STENOS_ERROR_DST_OVERFLOW;

		if STENOS_UNLIKELY (elements == 0)
			return 0;

		size_t stride = stenos_get_stride(ctx);
		if STENOS_UNLIKELY (stride == 0)
			return STENOS_ERROR_INVALID_PARAMETER;

		// Read level
		if (bytes == 0)
			return STENOS_ERROR_SRC_OVERFLOW;
		int level = (int)(*in++);
		if STENOS_UNLIKELY (level < 0 || level > 9)
			return STENOS_ERROR_INVALID_INPUT;

		--bytes;
		size_t step_size = 1ull << (17ull + (size_t)level / 2ull); // 130k minimum
		step_size = std::min(step_size, elements);

		std::vector<pixel_type> buf(step_size);

		size_t remaining = elements;
		while (remaining) {

			// Decompress frame
			stenos_decompress_ret r = stenos_consume_generic(ctx, in, sizeof(pixel_type), bytes, buf.data(), buf.size() * sizeof(pixel_type));
			if (stenos_has_error(r.decompressed_bytes))
				return r.decompressed_bytes;

			size_t samples = r.decompressed_bytes / sizeof(pixel_type);
			if STENOS_UNLIKELY (r.decompressed_bytes % sizeof(pixel_type))
				return STENOS_ERROR_INVALID_INPUT;

			if STENOS_UNLIKELY (samples == 0 && remaining != 0)
				return STENOS_ERROR_INVALID_INPUT;

			size_t values = undecimate_raw(buf.data(), samples, dst, dst_count, stride);
			if STENOS_UNLIKELY (stenos_has_error(values))
				return values;
			if STENOS_UNLIKELY (values > remaining)
				return STENOS_ERROR_INVALID_INPUT;

			dst += values * stride;
			dst_count -= values;
			in += r.consummed_bytes;
			bytes -= r.consummed_bytes;
			remaining -= values;
		}

		return elements;
	}

} // end stenos

size_t stenos_compress_numeric_generic(stenos_context* ctx, const void* src, stenos_numeric_type type, size_t count, void* dst, size_t dst_size)
{
	using namespace stenos;
	try {

		switch (type) {
			case StenosUInt8:
				return stenos_compress_numeric_type(ctx, (std::uint8_t*)src, count, dst, dst_size);
			case StenosInt8:
				return stenos_compress_numeric_type(ctx, (std::int8_t*)src, count, dst, dst_size);
			case StenosUInt16:
				return stenos_compress_numeric_type(ctx, (std::uint16_t*)src, count, dst, dst_size);
			case StenosInt16:
				return stenos_compress_numeric_type(ctx, (std::int16_t*)src, count, dst, dst_size);
			case StenosUInt32:
				return stenos_compress_numeric_type(ctx, (std::uint32_t*)src, count, dst, dst_size);
			case StenosInt32:
				return stenos_compress_numeric_type(ctx, (std::int32_t*)src, count, dst, dst_size);
			case StenosUInt64:
				return stenos_compress_numeric_type(ctx, (std::uint64_t*)src, count, dst, dst_size);
			case StenosInt64:
				return stenos_compress_numeric_type(ctx, (std::int64_t*)src, count, dst, dst_size);
			case StenosFloat32:
				return stenos_compress_numeric_type(ctx, (float*)src, count, dst, dst_size);
			case StenosFloat64:
				return stenos_compress_numeric_type(ctx, (double*)src, count, dst, dst_size);
			default:
				return STENOS_ERROR_INVALID_PARAMETER;
		}
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

size_t stenos_compress_numeric(const void* src, stenos_numeric_type type, size_t count, void* dst, size_t dst_size, double error, int level)
{
	stenos_context ctx;
	stenos_set_level(&ctx, level);
	stenos_set_maximum_error(&ctx, error);
	return stenos_compress_numeric_generic(&ctx, src, type, count, dst, dst_size);
}

size_t stenos_decompress_numeric_generic(stenos_context* ctx, const void* src, size_t bytes, void* dst, size_t dst_size, stenos_numeric_type dst_type)
{
	using namespace stenos;
	try {

		switch (dst_type) {
			case StenosUInt8:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::uint8_t*)dst, dst_size);
			case StenosInt8:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::int8_t*)dst, dst_size);
			case StenosUInt16:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::uint16_t*)dst, dst_size);
			case StenosInt16:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::int16_t*)dst, dst_size);
			case StenosUInt32:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::uint32_t*)dst, dst_size);
			case StenosInt32:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::int32_t*)dst, dst_size);
			case StenosUInt64:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::uint64_t*)dst, dst_size);
			case StenosInt64:
				return stenos_decompress_numeric_type(ctx, src, bytes, (std::int64_t*)dst, dst_size);
			case StenosFloat32:
				return stenos_decompress_numeric_type(ctx, src, bytes, (float*)dst, dst_size);
			case StenosFloat64:
				return stenos_decompress_numeric_type(ctx, src, bytes, (double*)dst, dst_size);
			default:
				return STENOS_ERROR_INVALID_PARAMETER;
		}
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

size_t stenos_decompress_numeric(const void* src, size_t bytes, void* dst, size_t dst_size, stenos_numeric_type dst_type)
{
	stenos_context ctx;
	return stenos_decompress_numeric_generic(&ctx, src, bytes, dst, dst_size, dst_type);
}