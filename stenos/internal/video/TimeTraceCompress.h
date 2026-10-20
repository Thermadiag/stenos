#pragma once

#include <utility>
#include <type_traits>
#include <iterator>
#include <algorithm>
#include <vector>
#include <fstream>
#include <limits>
#include <sstream>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <functional>

#include "../../bits.hpp"
#include "../../timer.hpp"
#include "../../stenos_video.h"
#include "../tiny_pool.h"

#ifdef STENOS_OPENCL
#include "TimeTraceCompressOpenCL.h"
#endif

namespace stenos
{
#pragma pack(1)
	// Pixel value and temporal position
	template<class T>
	struct PixelIndex
	{
		T value; // pixel value
		unsigned short index;
	};
#pragma pack()

	template<class T>
	struct MakeDouble
	{
		using type = std::conditional_t<sizeof(T) == 8 && std::is_integral_v<T> && (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits), long double, double>;
	};

	template<class T>
	class PixelData
	{
		using VType = PixelIndex<T>;
		using Index = unsigned short;
		// Best effort to handle 64 bits integer without precision loss: use long double (plateform/compiler dependant)
		using Double = typename MakeDouble<T>::type;

		std::vector<VType> points;
		std::vector<std::int64_t>* ptimes = nullptr;
		VType* buffer = nullptr;
		range<VType> last_pixels;
		std::int64_t candidate = -1;
		std::int64_t pos = 1;
		Double max_slope = -std::numeric_limits<Double>::infinity();
		Double min_slope = std::numeric_limits<Double>::infinity();
		bool finished = true;

		STENOS_ALWAYS_INLINE void clear_slopes() noexcept
		{
			max_slope = -std::numeric_limits<Double>::infinity();
			min_slope = std::numeric_limits<Double>::infinity();
		}

		STENOS_ALWAYS_INLINE int64_t time(const VType& v) const noexcept { return (*ptimes)[v.index]; }

		// Compute the slope between l and r
		static STENOS_ALWAYS_INLINE Double slope(Double l, Double r, Double dist) noexcept { return (r - l) / dist; }

		// Add last valid candidate to output points
		STENOS_ALWAYS_INLINE void update_last_pos() { points.push_back(last_pixels[candidate]); }

		// Process last added point
		STENOS_ALWAYS_INLINE void advance_min_max(double err)
		{
			const auto* val = &last_pixels[pos];
			const auto& first = last_pixels[0];

			// Compute slope
			// Subtract before conversion, without signed overflow across the timestamp origin.
			uint64_t diff_t = (uint64_t)time(*val) - (uint64_t)time(first);
			Double s = slope((Double)first.value, (Double)val->value, (Double)diff_t);

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
			// and update min/max slopes.
			diff_t = (uint64_t)time(*val) - (uint64_t)time(last_pixels[0]);
			Double upper_slope = slope((Double)last_pixels[0].value, (Double)val->value + err, (Double)diff_t);
			Double lower_slope = slope((Double)last_pixels[0].value, (Double)val->value - err, (Double)diff_t);
			min_slope = std::min(min_slope, upper_slope);
			max_slope = std::max(max_slope, lower_slope);
		}

	public:
		// Initialize with a value buffer and a time buffer.
		// Both buffers must have a size at least equal to the number
		// of values we are going to add.
		// The time buffer is shared, and the next pixel time must be updated
		// before the corresponding call to add().
		STENOS_ALWAYS_INLINE void init(VType* _buffer, std::vector<std::int64_t>* _times) noexcept
		{
			ptimes = _times;
			buffer = _buffer;
			last_pixels.reset(_buffer);
			points.clear();

			pos = 0;
			candidate = -1;
			finished = true;
			clear_slopes();
		}

		// Finish the decimation process.
		// Must be called before retrieving the decimated points with decimation().
		void finish(double error)
		{
			if (finished)
				return;

			// Finish

			// Commit the last valid endpoint and restart after the new anchor.
			if (pos >= (int64_t)(last_pixels.size()) - 1 && candidate != -1) {
				update_last_pos();
				last_pixels.erase(last_pixels.begin() + candidate);

				pos = 1; // Index 0 is the anchor, already emitted.
				candidate = -1;
				clear_slopes();
			}

			for (; pos < static_cast<std::int64_t>(last_pixels.size()); ++pos) {
				advance_min_max(error);

				// At the end of the pending range, commit the last valid endpoint.
				if (pos == (int64_t)(last_pixels.size()) - 1 && candidate != -1) {
					update_last_pos();
					last_pixels.erase(last_pixels.begin() + candidate);

					// The loop increment advances this to 1, past the new anchor.
					pos = 0;
					candidate = -1;
					clear_slopes();
				}
			}
			last_pixels.erase(last_pixels.end());
			clear_slopes();
			pos = 0;
			candidate = -1;
			finished = true;
		}

		// Add a new pixel value with its incrementing position (starting 0).
		STENOS_ALWAYS_INLINE void add(T value, unsigned short val_pos, double error)
		{
			if (error == 0) {
				last_pixels.push_back(VType{ value, val_pos });
				++pos;
				return;
			}

			if (!is_finite(value)) {
				finish(error);
				last_pixels.push_back(VType{ value, val_pos });
				// Preserve the raw sample, but exclude it from the next finite run.
				last_pixels.erase(last_pixels.end());
				points.push_back(VType{ value, val_pos });
				return;
			}

			last_pixels.push_back(VType{ value, val_pos });

			if (finished) {
				points.push_back(VType{ value, val_pos });
				pos = 1;
				candidate = -1;
				finished = false;
			}
			else {
				advance_min_max(error);
				++pos;
			}
		}

		STENOS_ALWAYS_INLINE std::vector<VType>& decimation() noexcept { return points; }
		STENOS_ALWAYS_INLINE const std::vector<VType>& decimation() const noexcept { return points; }
		STENOS_ALWAYS_INLINE void decimation(std::vector<VType>& out) noexcept { out = std::move(points); }
		STENOS_ALWAYS_INLINE VType* pixel_buffer() noexcept { return buffer; }
	};

	namespace compress_detail
	{

		static inline void write_uint64(std::string& out, uint64_t v)
		{
#if STENOS_BYTEORDER_ENDIAN == STENOS_BYTEORDER_BIG_ENDIAN
			v = stenos::byte_swap_64(v);
#endif
			out.insert(out.size(), (char*)&v, 8);
		}

		static inline uint64_t write_compressed_buffer(stenos_context* ctx, std::string& out, const void* data, size_t bytesoftype, size_t bytes)
		{
			std::vector<char> tmp(stenos_bound(bytes));
			size_t r = stenos_compress_generic(ctx, data, bytesoftype, bytes, tmp.data(), tmp.size());
			// The string-returning compression API propagates failures as exceptions.
			// Never use an encoded error as a byte count or emit a partial sub-buffer.
			if (stenos_has_error(r))
				throw std::runtime_error("Time trace compression failed");
			write_uint64(out, r);
			out.insert(out.size(), tmp.data(), r);

			return r + 8;
		}

		// Check the callback result: a failed read does not change the input pointer.
		static inline bool read_uint64(stenos_input* in, uint64_t& v)
		{
			v = 0;
			if (!in || !in->read || in->read((char*)&v, 8, in->opaque) != 8)
				return false;
#if STENOS_BYTEORDER_ENDIAN == STENOS_BYTEORDER_BIG_ENDIAN
			v = stenos::byte_swap_64(v);
#endif
			return true;
		}

		static inline stenos_context* get_context()
		{
			struct Context
			{
				stenos_context* ctx;
				Context()
				  : ctx(stenos_make_context())
				{
				}
				~Context() { stenos_destroy_context(ctx); }
			};
			thread_local Context c;
			return c.ctx;
		}

		template<class T>
		static inline bool read_compressed_vector(int threads, stenos_input* in, std::vector<T>& vec, uint64_t bytes)
		{
			stenos_context* ctx = get_context();
			stenos_set_threads(ctx, threads);
			uint64_t csize = 0;
			// Do not advertise more destination bytes than the vector actually owns.
			if (bytes % sizeof(T) || bytes > std::numeric_limits<size_t>::max() || !read_uint64(in, csize) || csize > std::numeric_limits<size_t>::max() ||
			    csize > (uint64_t)std::numeric_limits<int64_t>::max())
				return false;
			vec.resize((size_t)(bytes / sizeof(T)));
			std::vector<char> tmp(csize);
			if (in->read(tmp.data(), csize, in->opaque) != (int64_t)csize)
				return false;
			size_t r = stenos_decompress_generic(ctx, tmp.data(), sizeof(T), csize, vec.data(), bytes);
			return !stenos_has_error(r) && r == bytes;
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

	}

	class BaseTimeTraceCompress
	{
	public:
		BaseTimeTraceCompress() noexcept = default;
		virtual ~BaseTimeTraceCompress() noexcept = default;

		virtual void set_threads(int threads) noexcept = 0;
		virtual int threads() const noexcept = 0;

		virtual double error() const noexcept = 0;
		virtual void set_error(double error) noexcept = 0;

		virtual void set_compression_level(int level) noexcept = 0;
		virtual int compression_level() const noexcept = 0;

		virtual void set_max_time(uint64_t target) noexcept = 0;
		virtual uint64_t max_time() const noexcept = 0;

		virtual size_t max_GOP() const noexcept = 0;
		virtual void set_max_GOP(size_t gop) = 0;

		virtual int device() const noexcept = 0;

		virtual size_t width() const noexcept = 0;
		virtual size_t height() const noexcept = 0;

		virtual const std::vector<int64_t>& times() const noexcept = 0;
		virtual uint64_t current_pos() const noexcept = 0;

		virtual std::string add_frame(const void* img, std::int64_t time) = 0;
		virtual std::string add_frame_bytes(const void* img, int inner_stride_bytes, std::int64_t time) = 0;

		virtual std::string finish() = 0;
	};

	template<class T>
	class TimeTraceCompressFast : public BaseTimeTraceCompress
	{
	public:
		using PixelType = PixelIndex<T>;
		using FloatType = double;

	private:
		std::vector<T> d_tmp_img;
		std::vector<PixelData<T>> d_data;
		size_t d_width{ 0 };
		size_t d_height{ 0 };
		size_t d_max_gop{ 0 };
		size_t d_max_point_count{ 0 };
		double d_error{ 0 };
		double d_next_error{ 0 }; // Requested setting; d_error stays fixed during a GOP.
		int d_device = -1;
		uint64_t d_max_time = 0;
		unsigned short d_pos{ 0 };

		int d_threads{ 1 };
		int d_level{ 1 };
		stenos_context* d_ctx = stenos_make_context();

		std::vector<int64_t> d_times;
		std::vector<PixelType> d_buffer;

#ifdef STENOS_OPENCL
		// OpenCL implementation
		std::unique_ptr<TimeTraceCompressCL<T>> d_openCL;
#endif

		void insert_key_frame()
		{
			if (!d_error)
				return; // no decimation

			size_t size = d_width * d_height;

			// #pragma omp parallel for num_threads(d_threads)
			get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto p) {
				size_t pix = (size_t)p;
				PixelData<T>& d = this->d_data[pix];
				d.finish(d_error);
			});
		}

		void set_context_max_time(stenos::timer& timer, uint64_t& remaining_ns, uint64_t shift)
		{
			if (d_max_time) {
				auto elapsed_ns = timer.tock();
				uint64_t max_time = 1;
				if (elapsed_ns < remaining_ns)
					max_time = remaining_ns - elapsed_ns;
				max_time = max_time < (1ull << shift) ? 1ull : max_time >> shift;
				// printf("mtime: %f\n", max_time * 1e-9);
				stenos_set_max_nanoseconds(d_ctx, max_time);
			}
			else
				stenos_set_max_nanoseconds(d_ctx, 0);
		}

		std::string finish_internal()
		{
			stenos::timer timer;
			if (d_max_time)
				timer.tick();

			auto remaining_ns = d_max_time;

			set_context_max_time(timer, remaining_ns, 2);

			if (d_pos == 0)
				return std::string();

#ifdef STENOS_OPENCL
			if (d_openCL)
				d_openCL->finish_block();
			else
#endif
				insert_key_frame();

			// Only the CPU lossless path owns a complete contiguous d_buffer.
			bool direct_buffer = !d_error && d_pos == d_max_gop;
#ifdef STENOS_OPENCL
			if (d_openCL)
				direct_buffer = false;
#endif

			// write result
			std::string out;
			uint64_t full_size = 0;
			// write dummy full block size
			compress_detail::write_uint64(out, full_size);

			int level = d_level;
			if (level < 0)
				level = 0;
			if (level > 9)
				level = 9;

			// Write number of frame
			stenosv_block_header h;
			h.width = (uint16_t)d_width;
			h.height = (uint16_t)d_height;
			h.version = STENOS_VIDEO_TRACE_VERSION;
			h.count = (uint16_t)d_times.size();
			h.pixel_type = (unsigned char)to_numeric_type<T>();

			out.insert(out.size(), (char*)&h, sizeof(h));

			/*auto r =*/compress_detail::write_compressed_buffer(d_ctx, out, d_times.data(), 8, d_times.size() * 8);

			size_t total_pixels = 0;
			size_t size = d_height * d_width;

			std::vector<unsigned short> cnts_pix(size);
			std::vector<PixelType> pixels;

			if (!d_error) {

				// Directly take the raw points
				for (size_t i = 0; i < size; ++i) {
					cnts_pix[i] = (uint16_t)d_times.size();
					total_pixels += d_times.size();
#ifdef STENOS_OPENCL
					// Keep the selected backend even when the requested error is zero.
					if (d_openCL)
						d_openCL->retrieve_raw_pixels(i, (std::vector<CLPixelType<T>>&)pixels);
					else
#endif
					  if (!direct_buffer) {
						auto last_pixels = d_data[i].pixel_buffer();
						pixels.insert(pixels.end(), last_pixels, last_pixels + d_times.size());
					}
				}
			}
			else {
				// For each row, test the compressibility of raw and decimated points.
				std::vector<PixelType> raw(d_times.size() * d_width);
				std::vector<PixelType> dec(d_times.size() * d_width);

				std::vector<char> dst(dec.size() * sizeof(PixelType) * 2);
				for (size_t y = 0; y < d_height; ++y) {
					size_t dec_size = 0;

#ifdef STENOS_OPENCL
					if (d_openCL) {
						raw.clear();
						dec.clear();
					}
#endif

					for (size_t x = 0; x < d_width; ++x) {
#ifdef STENOS_OPENCL
						if (d_openCL) {
							size_t i = x + y * d_width;
							dec_size += d_openCL->retrieve_decimated_pixels(i, (std::vector<CLPixelType<T>>&)dec);
							d_openCL->retrieve_raw_pixels(i, (std::vector<CLPixelType<T>>&)raw);
						}
						else
#endif
						{
							size_t i = x + y * d_width;
							const auto& pts = d_data[i].decimation();
							auto* r = d_data[i].pixel_buffer();
							std::copy(pts.begin(), pts.end(), dec.begin() + dec_size);
							dec_size += pts.size();
							std::copy(r, r + d_times.size(), raw.data() + x * d_times.size());
						}
					}

					auto r_raw = stenos_private_assess_compressibility(raw.data(), sizeof(PixelType), raw.size() * sizeof(PixelType), dst.data());
					auto r_dec = stenos_private_assess_compressibility(dec.data(), sizeof(PixelType), dec_size * sizeof(PixelType), dst.data());

					if (r_dec < r_raw) {
						// printf("%f\n", (double)dec_size / (double)raw.size());
						//  Use decimated points
						pixels.insert(pixels.end(), dec.data(), dec.data() + dec_size);
						for (size_t x = 0; x < d_width; ++x) {
							size_t i = x + y * d_width;
#ifdef STENOS_OPENCL
							if (d_openCL) {
								cnts_pix[i] = (uint16_t)d_openCL->decimated_pixel_count(i);
								total_pixels += cnts_pix[i];
							}
							else
#endif
							{
								cnts_pix[i] = (uint16_t)d_data[i].decimation().size();
								total_pixels += d_data[i].decimation().size();
							}
						}
					}
					else {
						// Use raw points
						pixels.insert(pixels.end(), raw.data(), raw.data() + raw.size());
						for (size_t x = 0; x < d_width; ++x) {
							size_t i = x + y * d_width;
							cnts_pix[i] = (uint16_t)d_times.size();
							total_pixels += d_times.size();
						}
					}
				}
			}

			set_context_max_time(timer, remaining_ns, 2);

			// write number of points for each pixel trace
			/*r = */ compress_detail::write_compressed_buffer(d_ctx, out, cnts_pix.data(), sizeof(unsigned short), cnts_pix.size() * sizeof(unsigned short));

			// write total number of pixels
			uint64_t pix_count = pixels.size();
			if (direct_buffer)
				pix_count = d_buffer.size();

			compress_detail::write_uint64(out, pix_count * sizeof(PixelType));

			set_context_max_time(timer, remaining_ns, 0);

			// auto st = std::chrono::system_clock::now();
			if (direct_buffer) {
				// Directly use the internal buffer as it contains all pixels in the right order (no decimation)
				compress_detail::write_compressed_buffer(d_ctx, out, d_buffer.data(), sizeof(PixelType), d_buffer.size() * sizeof(PixelType));
			}
			else
				// write compressed pixels
				compress_detail::write_compressed_buffer(d_ctx, out, pixels.data(), sizeof(PixelType), pixels.size() * sizeof(PixelType));

			// TEST
			// auto el = std::chrono::system_clock::now() -st;
			// std::cout << "Compressing " << d_pos << " images took " << std::chrono::duration_cast<std::chrono::milliseconds>(el).count() / 1000. << " seconds" << std::endl;

			d_pos = 0;

			// Write block size
			full_size = (uint64_t)out.size() - 8;
			write_LE_64(out.data(), full_size);

			return out;
		}

		void init(size_t width, size_t height, double error)
		{
			// Header dimensions are uint16; thread-pool ranges use int.
			if (!width || !height || width > 65535 || height > 65535 || width > (size_t)std::numeric_limits<int>::max() / height)
				throw std::invalid_argument("Invalid video dimensions");
			d_width = width;
			d_height = height;
			if (!std::isfinite(error))
				throw std::invalid_argument("Non-finite error");
			d_data.resize(d_width * d_height);
			d_pos = 0;
			d_times.clear();
			if (error < 0)
				error = 0;
			if constexpr (std::is_integral_v<T>)
				error = std::floor(error);
			d_error = d_next_error = error; // Store the normalized value, not the original argument.
		}

	public:
		static_assert(std::is_arithmetic<T>::value, "invalid pixel type");

		TimeTraceCompressFast(size_t width, size_t height, double error) { init(width, height, error); }

		TimeTraceCompressFast(size_t width, size_t height, double error, size_t GOP, int threads, int device_id = -1)
		{
			if (!width || !height || width > 65535 || height > 65535 || width > (size_t)std::numeric_limits<int>::max() / height || !std::isfinite(error))
				throw std::invalid_argument("Invalid video dimensions or error");
			if (error < 0)
				error = 0;
			if constexpr (std::is_integral_v<T>)
				error = std::floor(error);
			d_device = device_id;
#ifdef STENOS_OPENCL
			if (device_id >= 0 && error) {
				d_openCL.reset(new TimeTraceCompressCL<T>((unsigned)device_id, width, height, error, GOP));
				if (d_openCL->is_valid()) {
					d_max_gop = d_openCL->gop(); // Use the backend's clamped GOP.
					d_error = d_next_error = error;
					set_threads(threads);
					d_width = width;
					d_height = height;
					d_pos = 0;
					return;
				}
				d_openCL.reset();
			}
#endif

			init(width, height, error);
			set_max_GOP(GOP);
			set_threads(threads);
		}

		TimeTraceCompressFast(const TimeTraceCompressFast&) = delete;
		TimeTraceCompressFast& operator=(const TimeTraceCompressFast&) = delete;

		~TimeTraceCompressFast() noexcept { stenos_destroy_context(d_ctx); }

		void set_threads(int threads) noexcept
		{
			d_threads = std::max(1, threads);
			stenos_set_threads(d_ctx, d_threads);
		}
		int threads() const noexcept { return d_threads; }

		double error() const noexcept { return d_next_error; }
		void set_error(double error) noexcept
		{
			// Queue changes until the next GOP; noexcept setters ignore invalid values.
			if (!std::isfinite(error))
				return;
			error = std::max(0.0, error);
			if constexpr (std::is_integral_v<T>)
				error = std::floor(error);
			d_next_error = error;
		}

		void set_max_time(uint64_t time_ns) noexcept { d_max_time = time_ns; }
		uint64_t max_time() const noexcept { return d_max_time; }

		void set_compression_level(int level) noexcept
		{
			d_level = level;
			stenos_set_level(d_ctx, level);
		}
		int compression_level() const noexcept { return d_level; }

		int device() const noexcept { return d_device; }

		size_t max_GOP() const noexcept { return d_max_gop; }
		void set_max_GOP(size_t gop)
		{
#ifdef STENOS_OPENCL
			if (d_openCL)
				return;
#endif
			if (d_pos || gop < 4) {
				// TODO: print error message
				return;
			}
			// Counts in the serialized format are uint16 (65536 is not representable).
			gop = std::min<size_t>(gop, 65535);
			if (gop > std::numeric_limits<size_t>::max() / sizeof(PixelType) / d_width / d_height)
				throw std::length_error("GOP allocation overflow");
			d_max_gop = gop;
			size_t total_pixels = gop * d_width * d_height;
			d_buffer.resize(total_pixels);

			size_t size = d_width * d_height;

			d_max_point_count = total_pixels / 16;
			if (d_max_point_count < size * 4)
				d_max_point_count = size * 4;
			if (d_max_point_count * sizeof(PixelType) > 1000000000ull) // avoid uncompressed size > 1GB
				d_max_point_count = 1000000000ull / sizeof(PixelType);
		}
		void set_max_memory(size_t bytes)
		{
			size_t frames = bytes / (d_width * d_height * sizeof(PixelType));
			if (frames < 4)
				frames = 4;
			set_max_GOP(frames);
		}

		size_t width() const noexcept { return d_width; }
		size_t height() const noexcept { return d_height; }

		const std::vector<PixelType>& pixels(size_t x, size_t y) const noexcept { return d_data[x + y * d_width].decimation(); }
		const std::vector<PixelType>& pixels(size_t i) const noexcept { return d_data[i].decimation(); }
		std::vector<PixelType>& pixels(size_t x, size_t y) noexcept { return d_data[x + y * d_width].decimation(); }
		std::vector<PixelType>& pixels(size_t i) noexcept { return d_data[i].decimation(); }

		std::string finish() { return finish_internal(); }

		virtual const std::vector<int64_t>& times() const noexcept { return d_times; }

		virtual uint64_t current_pos() const noexcept { return d_pos; }

		virtual std::string add_frame(const void* img, std::int64_t time) { return this->add_image(static_cast<const T*>(img), time); }

		virtual std::string add_frame_bytes(const void* img, int inner_stride_bytes, std::int64_t time)
		{
			return inner_stride_bytes <= 0 ? std::string() : this->add_image_bytes(img, (size_t)inner_stride_bytes, time);
		}

		std::string add_image_bytes(const void* _img, size_t inner_bytes, int64_t time)
		{
			if (!_img || inner_bytes < sizeof(T))
				return {};
			if (d_pos && time <= d_times.back())
				throw std::invalid_argument("Timestamps must strictly increase within a GOP");

			static constexpr size_t alignment = alignof(T);
			bool is_aligned = ((uintptr_t)_img % alignment) == 0 && (inner_bytes % alignment) == 0 && (inner_bytes % sizeof(T) == 0);
			size_t inner = is_aligned ? inner_bytes / sizeof(T) : 0;
			const T* t_img = static_cast<const T*>(_img);
			const char* c_img = static_cast<const char*>(_img);

			if (d_pos == 0) {
				d_times.clear();
				d_error = d_next_error;
#ifdef STENOS_OPENCL
				if (d_openCL)
					d_openCL->set_error(d_error);
#endif
			}

#ifdef STENOS_OPENCL

			if (d_openCL) {
				bool has_finish = false;
				if (is_aligned && inner == 1)
					has_finish = d_openCL->add_image(static_cast<const T*>(_img), time);
				else {
					d_tmp_img.resize(d_width * d_height);
					if (is_aligned) {
						for (size_t i = 0; i < d_tmp_img.size(); ++i)
							d_tmp_img[i] = t_img[i * inner];
					}
					else {
						for (size_t i = 0; i < d_tmp_img.size(); ++i) {
							T tmp;
							memcpy(&tmp, c_img + i * inner_bytes, sizeof(T));
							d_tmp_img[i] = tmp;
						}
					}
					has_finish = d_openCL->add_image(d_tmp_img.data(), time);
				}
				d_times.push_back(time);
				++d_pos;
				if (has_finish)
					return finish();
				return {};
			}

#endif
			d_times.push_back(time);
			if (d_max_gop == 0) {
				// initialize with 500MB
				set_max_memory(500000000u);
			}

			size_t size = d_width * d_height;
			auto* start = d_buffer.data();

			if (d_pos == 0) {
				// #pragma omp parallel for num_threads(d_threads)
				get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto _i) {
					size_t pix = (size_t)_i;
					T val;
					if (is_aligned)
						val = t_img[pix * inner];
					else
						memcpy(&val, c_img + pix * inner_bytes, sizeof(T));
					PixelData<T>& d = d_data[pix];

					d.init(start + pix * d_max_gop, &d_times);
					d.add(val, d_pos, d_error);
				});
				++d_pos;
				return std::string();
			}

			// #pragma omp parallel for num_threads(d_threads)
			get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto p) {
				size_t pix = (size_t)p;
				PixelData<T>& d = this->d_data[pix];

				T val;
				if (is_aligned)
					val = t_img[pix * inner];
				else
					memcpy(&val, c_img + pix * inner_bytes, sizeof(T));

				d.add(val, d_pos, d_error);
			});
			++d_pos;

			// compute total number of points
			// size_t count = 0;
			// for (size_t i = 0; i < size; ++i)
			//	count += d_data[i].points.size();

			if (d_pos >= 65535 || (d_pos >= d_max_gop) /* || count >= d_max_point_count*/) { // For now, ignore d_max_point_count as we might need deterministic GOP
				// printf("stop after %i frames\n", (int)d_pos);
				// printf("points: %i\n", (int)count);
				return finish();
			}

			return std::string();
		}

		std::string add_image(const T* _img, int64_t time) { return add_image_bytes(_img, sizeof(T), time); }
	};

	class BaseTimeTraceDecompressBlock
	{
	public:
		BaseTimeTraceDecompressBlock() noexcept = default;
		virtual ~BaseTimeTraceDecompressBlock() noexcept = default;

		virtual int threads() const noexcept = 0;
		virtual void set_threads(int threads) noexcept = 0;

		virtual const stenosv_block_header& header() const noexcept = 0;
		virtual const std::vector<int64_t>& times() const noexcept = 0;

		virtual bool open(stenos_input* in, bool read_data) = 0;
		virtual void close() = 0;

		virtual bool seek(int64_t time) = 0;
		virtual bool seek_pos(uint64_t pos) = 0;
		virtual bool read_bytes(void* img, size_t inner_bytes) = 0;
	};

	template<class T>
	class TimeTraceDecompressBlock : public BaseTimeTraceDecompressBlock
	{
	public:
#pragma pack(1)
		struct PixelType
		{
			T value;
			unsigned short index;
		};
#pragma pack()
		using TimeTrace = std::vector<PixelType>;
		using TimeTraceRange = std::pair<const PixelType*, const PixelType*>;

	private:
		std::vector<int64_t> d_times;
		stenosv_block_header d_header{};
		std::uint64_t d_pos{ 0 };
		std::vector<uint64_t> d_trace_pos;
		int d_threads{ 1 };

		std::vector<PixelType> d_time_traces;
		std::vector<std::pair<size_t, size_t>> d_poss;

		std::vector<PixelType> d_partial_time_traces;
		std::vector<std::pair<size_t, size_t>> d_partial_poss;

		// Validate records once on input; reconstruction can then use unchecked indices.
		bool valid_trace(const PixelType* tr, size_t count) const noexcept
		{
			if (!count || tr[0].index != 0 || tr[count - 1].index != d_times.size() - 1)
				return false;
			for (size_t i = 1; i < count; ++i) {
				if (tr[i].index <= tr[i - 1].index || tr[i].index >= d_times.size())
					return false;
				if ((!is_finite(tr[i - 1].value) || !is_finite(tr[i].value)) && tr[i].index != tr[i - 1].index + 1)
					return false;
			}
			return true;
		}

		STENOS_ALWAYS_INLINE T reconstruct(const PixelType &prev, const PixelType &next, size_t count, uint64_t& cursor) const noexcept
		{
			// Copy retained values as T: no NaN*0 and no 64-bit integer conversion loss.
			if (d_pos == next.index) {
				cursor += cursor < count - 1;
				return next.value;
			}
			if (d_pos == prev.index)
				return prev.value;

			using Double = typename MakeDouble<T>::type;
			// Subtract integer nanoseconds first, including across the signed origin.
			const uint64_t elapsed = (uint64_t)d_times[d_pos] - (uint64_t)d_times[prev.index];
			const uint64_t duration = (uint64_t)d_times[next.index] - (uint64_t)d_times[prev.index];
			const Double fraction = (Double)elapsed / (Double)duration;
			return compress_detail::cast_interpolate<T>((Double)next.value * fraction + (Double)prev.value * ((Double)1 - fraction));
		}

		bool open_skip_size(stenos_input* in, bool read_content = true)
		{
			if (!in)
				return false;

			if (in->read((char*)&d_header, sizeof(d_header), in->opaque) != sizeof(d_header)) {
				close();
				return false;
			}

			// Handle old format
			if (d_header.version == 0 && d_header.pixel_type == 0 && d_header.width && d_header.height && d_header.count) {
				d_header.version = STENOS_VIDEO_TRACE_VERSION;
				d_header.pixel_type = StenosUInt16;
			}

			if (d_header.pixel_type != to_numeric_type<T>() || d_header.version != STENOS_VIDEO_TRACE_VERSION || !d_header.width || !d_header.height || !d_header.count ||
			    (size_t)d_header.width > (size_t)std::numeric_limits<int>::max() / d_header.height)
				return false;

			d_times.resize(d_header.count);

			size_t size = (size_t)d_header.width * (size_t)d_header.height;

			// read times
			if (!compress_detail::read_compressed_vector(d_threads, in, d_times, d_header.count * 8)) {
				close();
				return false;
			}

			if (std::adjacent_find(d_times.begin(), d_times.end(), std::greater_equal<int64_t>()) != d_times.end()) {
				close();
				return false;
			}

			if (!read_content)
				return true;

			// Read ALL sizes
			std::vector<unsigned short> cnts_pix(size);
			if (!compress_detail::read_compressed_vector(d_threads, in, cnts_pix, size * 2)) {
				close();
				return false;
			}

			uint64_t pix_bytes = 0, expected_records = 0;
			for (auto count : cnts_pix) {
				if (!count || count > d_times.size()) {
					close();
					return false;
				}
				expected_records += count;
			}
			if (expected_records > std::numeric_limits<size_t>::max() / sizeof(PixelType) || !compress_detail::read_uint64(in, pix_bytes) ||
			    pix_bytes != expected_records * sizeof(PixelType)) {
				close();
				return false;
			}
			d_time_traces.clear();
			if (!compress_detail::read_compressed_vector(d_threads, in, d_time_traces, pix_bytes) || d_time_traces.size() < cnts_pix.size()) {
				close();
				return false;
			}
			d_poss.resize(cnts_pix.size());
			size_t cum_count = 0;
			for (size_t i = 0; i < cnts_pix.size(); ++i) {
				d_poss[i] = { cum_count, cnts_pix[i] };
				if (!valid_trace(d_time_traces.data() + cum_count, cnts_pix[i])) {
					close();
					return false;
				}
				cum_count += cnts_pix[i];
			}
			d_pos = 0;
			return true;
		}

		struct Istream
		{
			stenos_input* in = nullptr;
			int64_t start = 0;
			int64_t pos = 0;
			stenos_lock* lock; // start unlocked
		};
		struct Locker
		{
			stenos_lock* lock;
			Locker(stenos_lock* l)
			  : lock(l)
			{
				if (l)
					l->lock(l->opaque);
			}
			~Locker()
			{
				if (lock)
					lock->unlock(lock->opaque);
			}
		};
		struct Unlocker
		{
			stenos_lock* lock;
			Unlocker(stenos_lock* l)
			  : lock(l)
			{
				if (l)
					l->unlock(l->opaque);
			}
			~Unlocker()
			{
				if (lock)
					lock->lock(lock->opaque);
			}
		};
		static int64_t read_stream(char* dst, int64_t size, void* opaque)
		{
			Istream* iss = static_cast<Istream*>(opaque);
			Locker lock(iss->lock);
			auto pos = iss->in->seek(iss->start + iss->pos, STENOS_SEEK_SET, iss->in->opaque);
			if (pos < 0)
				return (int64_t)STENOS_ERROR_INVALID_IO;
			if ((int64_t)iss->in->read(dst, size, iss->in->opaque) != size)
				return (int64_t)STENOS_ERROR_INVALID_IO;

			iss->pos += size;
			return size;
		}
		static int64_t seek_stream(int64_t pos, int whence, void* opaque)
		{
			Istream* iss = static_cast<Istream*>(opaque);
			if (whence == STENOS_SEEK_SET) {
				if (pos < 0)
					return STENOS_ERROR_INVALID_IO;
				iss->pos = (int64_t)pos;
			}
			else {
				iss->pos += pos;
				if (iss->pos < 0)
					return STENOS_ERROR_INVALID_IO;
			}
			return 0;
		}
		static int64_t tell_stream(void* opaque) { return static_cast<Istream*>(opaque)->pos; }

		std::vector<TimeTraceRange> open_skip_size_partial(stenos_input* in, const stenosv_coordinate* coords, size_t count, stenos_lock* lock)
		{
			// Extract given time traces without opening the block.
			// This performs a partial decompression of the block,
			// and supports parallel IO on input stenos_input.

			std::vector<TimeTraceRange> res;

			if (!in)
				return res;

			if (in->read((char*)&d_header, sizeof(d_header), in->opaque) != sizeof(d_header)) {
				close();
				return res;
			}

			// Handle old format
			if (d_header.count && d_header.height && d_header.width && d_header.version == 0 && d_header.pixel_type == 0) {
				d_header.version = STENOS_VIDEO_TRACE_VERSION;
				d_header.pixel_type = StenosUInt16;
			}

			if (d_header.pixel_type != to_numeric_type<T>() || d_header.version != STENOS_VIDEO_TRACE_VERSION || !d_header.width || !d_header.height || !d_header.count ||
			    (size_t)d_header.width > (size_t)std::numeric_limits<int>::max() / d_header.height) {
				close();
				return res;
			}

			d_times.resize(d_header.count);

			size_t size = (size_t)d_header.width * (size_t)d_header.height;

			// read times
			if (!compress_detail::read_compressed_vector(d_threads, in, d_times, d_header.count * 8)) {
				close();
				return res;
			}

			if (std::adjacent_find(d_times.begin(), d_times.end(), std::greater_equal<int64_t>()) != d_times.end()) {
				close();
				return res;
			}

			// Read ALL sizes
			std::vector<unsigned short> cnts_pix(size);
			if (!compress_detail::read_compressed_vector(d_threads, in, cnts_pix, size * 2)) {
				close();
				return res;
			}

			uint64_t pix_bytes = 0, csize = 0, expected_records = 0;
			for (auto n : cnts_pix) {
				if (!n || n > d_times.size()) {
					close();
					return res;
				}
				expected_records += n;
			}
			if (expected_records > std::numeric_limits<size_t>::max() / sizeof(PixelType) || !compress_detail::read_uint64(in, pix_bytes) ||
			    pix_bytes != expected_records * sizeof(PixelType) || !compress_detail::read_uint64(in, csize)) {
				close();
				return res;
			}
			int64_t stream_pos = (int64_t)in->tell(in->opaque);

			// Unlock the lock during heavy computation, relock at the end
			Unlocker unlock(lock);

			d_poss.resize(cnts_pix.size());
			size_t cum_count = 0;
			for (size_t i = 0; i < cnts_pix.size(); ++i) {
				d_poss[i] = { cum_count, cnts_pix[i] };
				cum_count += cnts_pix[i];
			}
			d_partial_poss.resize(count);
			d_partial_time_traces.clear();

			// Build the ranges to read
			std::vector<std::pair<size_t, size_t>> ranges(count);
			size_t partial_pixels = 0;
			for (size_t i = 0; i < count; ++i) {
				auto& c = coords[i];
				if STENOS_UNLIKELY (c.x > d_header.width || c.y > d_header.height) {
					close();
					return {};
				}
				unsigned pos = c.x + c.y * d_header.width;
				ranges[i] = { d_poss[pos].first, d_poss[pos].first + d_poss[pos].second };
				d_partial_poss[i] = { partial_pixels, d_poss[pos].second };
				if (d_poss[pos].second > std::numeric_limits<size_t>::max() / sizeof(PixelType) - partial_pixels) {
					close();
					return {};
				}
				partial_pixels += d_poss[pos].second;
			}
			d_partial_time_traces.resize(partial_pixels);

			{
				// Partial decompression
				stenos_context* ctx = compress_detail::get_context();

				Istream iss{ in, stream_pos, 0, (lock) };
				stenos_input io;
				io.read = read_stream;
				io.seek = seek_stream;
				io.tell = tell_stream;
				io.opaque = &iss;
				size_t r = stenos_decompress_sub_part(
				  ctx, &io, sizeof(PixelType), d_partial_time_traces.data(), d_partial_time_traces.size() * sizeof(PixelType), (size_t*)ranges.data(), ranges.size());
				if (stenos_has_error(r) || r != partial_pixels * sizeof(PixelType)) {
					close();
					return res;
				}
			}

			res.resize(count);
			for (size_t i = 0; i < count; ++i) {
				const auto* start = d_partial_time_traces.data() + d_partial_poss[i].first;
				if (!valid_trace(start, d_partial_poss[i].second)) {
					close();
					return {};
				}
				res[i] = { start, start + d_partial_poss[i].second };
			}

			return res;
		}

	public:
		static_assert(std::is_arithmetic<T>::value, "invalid pixel type");

		TimeTraceDecompressBlock() noexcept = default;
		TimeTraceDecompressBlock(const TimeTraceDecompressBlock&) = default;
		TimeTraceDecompressBlock(TimeTraceDecompressBlock&&) noexcept = default;
		TimeTraceDecompressBlock& operator=(const TimeTraceDecompressBlock&) = default;
		TimeTraceDecompressBlock& operator=(TimeTraceDecompressBlock&&) noexcept = default;

		int threads() const noexcept { return d_threads; }
		void set_threads(int threads) noexcept { d_threads = std::max(1, threads); }

		bool empty() const noexcept { return d_times.empty() || d_time_traces.empty(); }
		const stenosv_block_header& header() const noexcept { return d_header; }
		const std::vector<int64_t>& times() const noexcept { return d_times; }
		int64_t time() const noexcept { return d_times.empty() ? 0 : d_times[0]; }
		std::pair<int64_t, int64_t> bounds() const noexcept { return d_times.empty() ? std::pair<int64_t, int64_t>{ 0ll, 0ll } : std::pair<int64_t, int64_t>{ d_times[0], d_times.back() }; }
		TimeTraceRange time_trace(unsigned x, unsigned y) const
		{
			if (x >= d_header.width || y >= d_header.height || d_time_traces.empty())
				return { nullptr, nullptr };
			size_t pos = x + (size_t)y * d_header.width;
			if (pos >= d_poss.size())
				return { nullptr, nullptr };
			const auto* start = d_time_traces.data() + d_poss[pos].first;
			return { start, start + d_poss[pos].second };
		}
		/*std::vector<TimeTraceRange> time_trace2(const stenosv_coordinate * coords, size_t count) const
		{
			std::vector<TimeTraceRange> res(count);
			for(size_t i = 0; i < count; ++i){
				const auto* start = d_time_traces.data() + d_partial_poss[i].first;
				res[i] =  { start, start + d_partial_poss[i].second };
			}
			return res;
		}*/
		void close()
		{
			memset((void*)&d_header, 0, sizeof(d_header));
			d_times.clear();
			d_times.shrink_to_fit();

			d_trace_pos.clear();
			d_trace_pos.shrink_to_fit();

			d_time_traces.clear();
			d_time_traces.shrink_to_fit();

			d_poss.clear();
			d_poss.shrink_to_fit();

			d_partial_time_traces.clear();
			d_partial_time_traces.shrink_to_fit();

			d_partial_poss.clear();
			d_partial_poss.shrink_to_fit();

			d_pos = 0;
		}
		bool open(stenos_input* in, bool read_content = true)
		{
			close();
			uint64_t full_block_size = 0;
			if (!compress_detail::read_uint64(in, full_block_size))
				return false;
			auto pos = in->tell(in->opaque);
			if (pos < 0 || full_block_size > (uint64_t)std::numeric_limits<int64_t>::max() - (uint64_t)pos)
				return false;
			if (!open_skip_size(in, read_content))
				return false;
			if (!read_content) {
				if (in->seek(pos + (int64_t)full_block_size, STENOS_SEEK_SET, in->opaque) < 0)
					return false;
			}
			return true;
		}

		/* bool open_parallel(uint64_t seek_position, ViewStreamBuff& in, bool read_content = true)
		{
			close();

			const char* src = in.data() + seek_position;
			uint64_t full_block_size = stenos::read_LE_64(src);
			src += 8;

			ViewStream iss(src, full_block_size);
			return open_skip_size(iss, read_content);
		}*/

		/* bool open_parallel(uint64_t seek_position, stenos_input* in, bool read_content = true)
		{
			close();

			static std::mutex ll;

			std::unique_lock<std::mutex> lock(ll);

			if (in->seek(seek_position, in->opaque) < 0)
				return false;

			uint64_t full_block_size = compress_detail::read_uint64(in);

			std::string str(full_block_size, (char)0);
			if(in->read((char*)str.data(), full_block_size, in->opaque) != full_block_size)
				return false;

			ViewStream iss(str.data(), full_block_size);

			lock.unlock();

			return open_skip_size(iss, read_content);
		}*/

		std::vector<TimeTraceRange> open_parallel_partial(stenos_input* in, const stenosv_coordinate* coords, size_t count, stenos_lock* lock = nullptr)
		{
			close();

			uint64_t full_block_size = 0;
			if (!compress_detail::read_uint64(in, full_block_size) || (count && !coords))
				return {};
			return open_skip_size_partial(in, coords, count, lock);
		}

		virtual bool seek_pos(uint64_t pos)
		{
			if (empty() || pos >= (size_t)d_times.size())
				return false;
			if (pos == d_pos)
				return true;

			d_pos = pos;
			auto time = d_times[pos];

			size_t size = (size_t)d_header.width * d_header.height;
			if (d_trace_pos.empty())
				d_trace_pos.resize(size, 0);

			get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto _i) {
				size_t i = (size_t)_i;
				const size_t tr_size = d_poss[i].second;
				const PixelType* tr = d_time_traces.data() + d_poss[i].first;
				auto it = std::lower_bound(tr, tr + tr_size, time, [this](const auto& l, const auto& r) { return this->times()[l.index] < r; });
				if (it == tr + tr_size)
					--it;
				d_trace_pos[i] = it - tr;
				if (d_trace_pos[i] > 0)
					--d_trace_pos[i];
			});
			return true;
		}

		virtual bool seek(int64_t time)
		{
			if (d_times.empty())
				return false;

			auto pos = std::lower_bound(d_times.begin(), d_times.end(), time);
			if (pos == d_times.end())
				--pos;
			return seek_pos((uint64_t)(pos - d_times.begin()));
		}

		virtual bool read_bytes(void* img, size_t inner_bytes)
		{
			if (empty() || d_pos >= d_times.size() || !img)
				return false;

			if (inner_bytes < sizeof(T))
				return false;

			if (d_trace_pos.empty())
				d_trace_pos.resize((size_t)d_header.width * d_header.height, 0);

			size_t size = (size_t)d_header.width * d_header.height;
			size_t pixels_per_thread = size / (size_t)d_threads;
			size_t remaining = size % (size_t)d_threads;

			char* c_img = static_cast<char*>(img);

			get_pool().loop_for(d_threads, 0, d_threads, 1, [&](auto i) {
				size_t first = (size_t)i * pixels_per_thread;
				size_t end = first + ((i == d_threads - 1) ? (pixels_per_thread + remaining) : pixels_per_thread);
				//size_t count = end - first;
				size_t j = first;

				char* dst = c_img + j * inner_bytes;
				
				for (; j < end; ++j, dst += inner_bytes) {
					const PixelType* tr = d_time_traces.data() + d_poss[j].first;
					const size_t tr_size = d_poss[j].second;
					const size_t pos = this->d_trace_pos[j];
					const auto prev_val = tr[pos];
					const auto next_val = (pos == tr_size - 1) ? tr[pos] : tr[pos + 1];

					T value = reconstruct(prev_val, next_val, tr_size, this->d_trace_pos[j]);
					memcpy(dst, &value, sizeof(T));
				}
			});

			++d_pos;
			return true;
		}
	};

} // end namespace stenos
