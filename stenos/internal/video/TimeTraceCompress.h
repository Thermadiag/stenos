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

#include "../../bits.hpp"
#include "../../timer.hpp"
#include "../../stenos_video.h"
#include "../tiny_pool.h"

#ifdef STENOS_OPENCL
#include "TimeTraceCompressOpenCL.h"
#endif

namespace stenos
{

	namespace compress_detail
	{
		template<class T>
		class StaticFifo
		{
			T* d_data{ nullptr };
			T* d_buffer{ nullptr };
			size_t d_size{ 0 };
			size_t d_buffer_capacity{ 0 };

			static constexpr bool valid_type = std::is_trivially_copyable<T>::value && std::is_trivially_destructible<T>::value;

		public:
			static_assert(valid_type, "invalid type for StaticFifo");
			using value_type = T;
			using iterator = T*;
			using const_iterator = const T*;

			StaticFifo() noexcept = default;
			~StaticFifo() noexcept = default;

			void set_buffer(T* buffer, size_t capacity) noexcept
			{
				d_buffer = d_data = buffer;
				d_buffer_capacity = capacity;
			}

			STENOS_ALWAYS_INLINE size_t size() const noexcept { return d_size; }
			STENOS_ALWAYS_INLINE size_t capacity() const noexcept { return d_buffer_capacity - (d_data - d_buffer); }
			STENOS_ALWAYS_INLINE bool full() const noexcept { return size() == capacity(); }
			STENOS_ALWAYS_INLINE T* data() noexcept { return d_data; }
			STENOS_ALWAYS_INLINE const T* data() const noexcept { return d_data; }
			STENOS_ALWAYS_INLINE T* buffer() noexcept { return d_buffer; }
			STENOS_ALWAYS_INLINE const T* buffer() const noexcept { return d_buffer; }
			STENOS_ALWAYS_INLINE T* begin() noexcept { return d_data; }
			STENOS_ALWAYS_INLINE const T* begin() const noexcept { return d_data; }
			STENOS_ALWAYS_INLINE T* end() noexcept { return d_data + d_size; }
			STENOS_ALWAYS_INLINE const T* end() const noexcept { return d_data + d_size; }
			STENOS_ALWAYS_INLINE T& operator[](size_t i) noexcept { return d_data[i]; }
			STENOS_ALWAYS_INLINE const T& operator[](size_t i) const noexcept { return d_data[i]; }

			STENOS_ALWAYS_INLINE T& back() noexcept { return d_data[d_size - 1]; }
			STENOS_ALWAYS_INLINE const T& back() const noexcept { return d_data[d_size - 1]; }

			STENOS_ALWAYS_INLINE void push_back(const T& v)
			{
				if STENOS_UNLIKELY (full())
					throw std::runtime_error("push_back : StaticFifo is full");
				new (d_data + d_size) T(v);
				++d_size;
			}
			STENOS_ALWAYS_INLINE void erase(const_iterator start, const_iterator end)
			{
				if (start != d_data)
					throw std::runtime_error("StaticFifo can only erase from the beginning");

				d_size -= (end - start);
				d_data += (end - start);
			}
			STENOS_ALWAYS_INLINE void clear() noexcept
			{
				d_size = 0;
				d_data = d_buffer;
			}
			STENOS_ALWAYS_INLINE void shrink_to_fit() noexcept {}
		};

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
			write_uint64(out, r);
			out.insert(out.size(), tmp.data(), r);

			if (stenos_has_error(r))
				return r;

			return r + 8;
		}

		static inline uint64_t read_uint64(stenos_input* in)
		{
			uint64_t v = 0;
			in->read((char*)&v, 8, in->opaque);
#if STENOS_BYTEORDER_ENDIAN == STENOS_BYTEORDER_BIG_ENDIAN
			v = stenos::byte_swap_64(v);
#endif
			return v;
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
			size_t csize = read_uint64(in);
			vec.resize(bytes / sizeof(T));
			std::vector<char> tmp(csize);
			if (in->read(tmp.data(), csize, in->opaque) != csize)
				return false;
			size_t r = stenos_decompress_generic(ctx, tmp.data(), sizeof(T), csize, vec.data(), bytes);
			return stenos_has_error(r) == 0;
		}

		template<class T>
		static STENOS_ALWAYS_INLINE T fround_to(double v) noexcept
		{
			if (std::is_integral<T>::value)
				return (T)std::llrint(v);
			return (T)v;
		}

	}

#pragma pack(1)

	// Pixel value and temporal position
	template<class T>
	struct PixelIndex
	{
		T value; // pixel value
		unsigned short index;
	};

#pragma pack()

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
		static constexpr size_t MaxSpace = (size_t)-1;
		using PixelTypeVector = compress_detail::StaticFifo<PixelType>;
		struct PixelData
		{
			std::vector<PixelType> points;
			PixelTypeVector last_pixels;
			size_t candidate;
			size_t pos = 1;
			size_t start = 0;
		};

		std::vector<T> d_tmp_img;
		std::vector<PixelData> d_data;
		size_t d_width{ 0 };
		size_t d_height{ 0 };
		size_t d_max_gop{ 0 };
		size_t d_max_point_count{ 0 };
		double d_error{ 0 };
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

		STENOS_ALWAYS_INLINE int64_t time(const PixelType& p) const noexcept { return d_times[p.index]; }

		static STENOS_ALWAYS_INLINE FloatType slope(FloatType l, FloatType r, FloatType dist) noexcept { return (r - l) / dist; }
		template<class U>
		static STENOS_ALWAYS_INLINE U dabs(U v) noexcept
		{
			return v < 0 ? -v : v;
		}
		static STENOS_ALWAYS_INLINE FloatType dmax(FloatType v1, FloatType v2) noexcept { return v1 > v2 ? v1 : v2; }
		static STENOS_ALWAYS_INLINE bool dcompare(double p1, double p2) noexcept { return (dabs(p1 - p2) * 1000000000000. <= std::min(dabs(p1), dabs(p2))); }
		static STENOS_ALWAYS_INLINE bool dcompare(float p1, float p2) { return (dabs(p1 - p2) * 100000.f <= std::min(dabs(p1), dabs(p2))); }

		/*static STENOS_ALWAYS_INLINE double hmax(__m256d x) noexcept
		{
			__m256d y = _mm256_permute2f128_pd(x, x, 1); // permute 128-bit values
			__m256d m1 = _mm256_max_pd(x, y); // m1[0] = max(x[0], x[2]), m1[1] = max(x[1], x[3]), etc.
			__m256d m2 = _mm256_permute_pd(m1, 5); // set m2[0] = m1[1], m2[1] = m1[0], etc.
			__m256d m = _mm256_max_pd(m1, m2);
			double v[4];
			_mm256_storeu_pd(v, m);
			return v[0];
		}*/

		FloatType check_candidate_min_max(const PixelTypeVector& last_pixels, size_t end, FloatType s, FloatType error2) const noexcept
		{
			FloatType error_max = -std::numeric_limits<FloatType>::infinity();
			if (1 == end)
				return error_max;

			const FloatType beta = last_pixels[0].value - s * time(last_pixels[0]);
			const auto* p = last_pixels.data() + 1;
			const auto* pend = last_pixels.data() + end;

			/*if (p + 3 < pend) {
				const __m256d betas = _mm256_set1_pd(beta);
				const __m256d ss = _mm256_set1_pd(s);
				const __m256d sign_mask = _mm256_set1_pd(-0.);

				while (p + 3 < pend) {
					__m256d theoric = _mm256_setr_pd((double)time(p[0]), (double)time(p[0 + 1]), (double)time(p[0 + 2]), (double)time(p[0 + 3]));
					__m256d vals = _mm256_setr_pd((double)p[0].value, (double)p[0 + 1].value, (double)p[0 + 2].value, (double)p[0 + 3].value);
					theoric = _mm256_add_pd(betas, _mm256_mul_pd(ss, theoric));
					vals = _mm256_andnot_pd(sign_mask, _mm256_sub_pd(vals, theoric));
					error_max = std::max(error_max,hmax(vals));
					if (error_max > error2) // check early stop
						return error_max;
					p += 4;
				}
			}*/

			while (p + 3 < pend) {
				FloatType theoric_y1 = s * (FloatType)time(p[0]) + beta;
				FloatType theoric_y2 = s * (FloatType)time(p[1]) + beta;
				FloatType theoric_y3 = s * (FloatType)time(p[2]) + beta;
				FloatType theoric_y4 = s * (FloatType)time(p[3]) + beta;

				theoric_y1 = dabs((FloatType)p[0].value - theoric_y1);
				theoric_y2 = dabs((FloatType)p[1].value - theoric_y2);
				theoric_y3 = dabs((FloatType)p[2].value - theoric_y3);
				theoric_y4 = dabs((FloatType)p[3].value - theoric_y4);

				theoric_y1 = dmax(theoric_y1, theoric_y2);
				theoric_y3 = dmax(theoric_y3, theoric_y4);
				error_max = dmax(error_max, dmax(theoric_y1, theoric_y3));
				if (error_max > error2) // check early stop
					return error_max;
				p += 4;
			}

			for (; p < pend; ++p) {
				FloatType theoric_y = s * (FloatType)time(*p) + beta;
				FloatType err = dabs((FloatType)p->value - theoric_y);
				error_max = dmax(error_max, err);
			}
			return error_max;
		}
		static STENOS_ALWAYS_INLINE bool inf_equal(FloatType a, FloatType b)
		{
			if (dcompare(a, b))
				return true;
			return a < b;
		}

		void advance_min_max(PixelData& d, bool key)
		{
			const FloatType error2 = (FloatType)d_error * (FloatType)2.00001;
			const auto val = d.last_pixels[d.pos];

			// compute slope
			FloatType s = slope((FloatType)d.last_pixels[0].value, (FloatType)val.value, (FloatType)(time(val) - time(d.last_pixels[0])));

			// check points before
			FloatType error_max = std::numeric_limits<FloatType>::infinity();
			if (d_error != 0 && !key) // if d_error is 0, it is more efficient to just NOT try to remove points
				error_max = check_candidate_min_max(d.last_pixels, d.pos, s, error2);

			if (error_max > error2) {
				// stop here
				if (d.candidate == MaxSpace)
					d.candidate = d.pos;

				d.points.push_back(d.last_pixels[d.candidate]);
				d.last_pixels.erase(d.last_pixels.begin(), d.last_pixels.begin() + d.candidate);
				d.pos = 0;
				d.candidate = MaxSpace;
			}
			else if (inf_equal(error_max, d_error)) {
				d.candidate = d.pos;
			}
		}

		void insert_key_frame(const std::vector<size_t>& key_pixels = std::vector<size_t>())
		{
			size_t size = d_width * d_height;
			if (!key_pixels.empty())
				size = key_pixels.size();

			// #pragma omp parallel for num_threads(d_threads)
			get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto p) {
				size_t pix = (size_t)p;
				if (!key_pixels.empty())
					pix = key_pixels[pix];

				PixelData& d = this->d_data[pix];

				if (d.pos >= d.last_pixels.size() - 1) {
					// we reach the end
					if (d.candidate != MaxSpace) {
						// add previous candidate
						d.points.push_back(d.last_pixels[d.candidate]);
						d.last_pixels.erase(d.last_pixels.begin(), d.last_pixels.begin() + d.candidate);
						d.pos = 0;
						d.candidate = (size_t)-1;
					}
				}

				for (; d.pos != d.last_pixels.size(); ++d.pos) {
					advance_min_max(d, false);
					if (d.pos >= d.last_pixels.size() - 1) {
						// we reach the end
						if (d.candidate != MaxSpace) {
							// add previous candidate
							if (d.points.back().index != d_pos - 1) {
								d.points.push_back(d.last_pixels[d.candidate]);
								d.last_pixels.erase(d.last_pixels.begin(), d.last_pixels.begin() + d.candidate);
								d.pos = 0;
								d.candidate = (size_t)-1;
							}
						}
					}
				}

				// Add last point
				if (d.points.back().index != d_pos - 1)
					d.points.push_back(d.last_pixels.back());
			});
		}

		void set_context_max_time(stenos::timer & timer, uint64_t & remaining_ns, uint64_t shift)
		{
			if (d_max_time) {
				auto elapsed_ns = timer.tock();
				uint64_t max_time = 1;
				if (elapsed_ns < remaining_ns)
					max_time = remaining_ns - elapsed_ns;
				max_time = max_time < (1 << shift) ? 1 : max_time >> shift;
				//printf("mtime: %f\n", max_time * 1e-9);
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
			h.pixel_type = (unsigned char)stenosv_to_pixel_type<T>();

			out.insert(out.size(), (char*)&h, sizeof(h));

			/*auto r =*/compress_detail::write_compressed_buffer(d_ctx, out, d_times.data(), 8, d_times.size() * 8);

			size_t total_pixels = 0;
			size_t size = d_height * d_width;

			std::vector<unsigned short> cnts_pix(size);
			std::vector<PixelType> pixels;
			if (!d_error || d_level < 3) {
				// Directly take the decimated points
#ifdef STENOS_OPENCL
				if (d_openCL) {
					// stenos::timer t;
					// t.tick();
					for (size_t i = 0; i < size; ++i) {
						cnts_pix[i] = (uint16_t)d_openCL->retrieve_decimated_pixels(i, (std::vector<CLPixelType<T>>&)pixels);
						total_pixels += cnts_pix[i];

					}
					// auto el = t.tock();
					// printf("%f\n", (el * 1e-6));
				}
				else
#endif
				{
					for (size_t i = 0; i < size; ++i) {
						cnts_pix[i] = (uint16_t)d_data[i].points.size();
						total_pixels += d_data[i].points.size();
						pixels.insert(pixels.end(), d_data[i].points.begin(), d_data[i].points.end());
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
							auto& pts = d_data[i].points;
							auto* r = d_data[i].last_pixels.buffer();
							std::copy(pts.begin(), pts.end(), dec.begin() + dec_size);
							dec_size += pts.size();
							std::copy(r, r + d_times.size(), raw.data() + x * d_times.size());
						}
					}

					auto r_raw = stenos_private_assess_compressibility(raw.data(), sizeof(PixelType), raw.size() * sizeof(PixelType), dst.data());
					auto r_dec = stenos_private_assess_compressibility(dec.data(), sizeof(PixelType), dec_size * sizeof(PixelType), dst.data());

					if ( r_dec < r_raw) {
						//printf("%f\n", (double)dec_size / (double)raw.size());
						// Use decimated points
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
								cnts_pix[i] = (uint16_t)d_data[i].points.size();
								total_pixels += d_data[i].points.size();
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
			compress_detail::write_uint64(out, pixels.size() * sizeof(PixelType));

			set_context_max_time(timer, remaining_ns, 0);

			// write compressed pixels
			/*r =*/compress_detail::write_compressed_buffer(d_ctx, out, pixels.data(), sizeof(PixelType), pixels.size() * sizeof(PixelType));

			d_pos = 0;

			// Write block size
			full_size = (uint64_t)out.size() - 8;
			write_LE_64(out.data(), full_size);

			return out;
		}

		void init(size_t width, size_t height, double error)
		{
			d_width = width;
			d_height = height;
			d_error = (error);
			d_data.resize(d_width * d_height);
			d_pos = 0;
			d_times.clear();
		}

	public:
		static_assert(std::is_arithmetic<T>::value, "invalid pixel type");

		TimeTraceCompressFast(size_t width, size_t height, double error) { init(width, height, error); }

		TimeTraceCompressFast(size_t width, size_t height, double error, size_t GOP, int threads, int device_id = -1)
		{
			d_device = device_id;
#ifdef STENOS_OPENCL
			if (device_id >= 0) {
				d_openCL.reset(new TimeTraceCompressCL<T>((unsigned)device_id, width, height, error, GOP));
				if (d_openCL->is_valid()) {
					d_max_gop = GOP;
					d_error = error;
					d_threads = threads;
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
			d_threads = threads;
			stenos_set_threads(d_ctx, threads);
		}
		int threads() const noexcept { return d_threads; }

		double error() const noexcept { return d_error; }
		void set_error(double error) noexcept
		{
			d_error = error;
#ifdef STENOS_OPENCL
			if (d_openCL)
				d_openCL->set_error(error);
#endif
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
			d_max_gop = gop;
			size_t total_pixels = gop * d_width * d_height;
			d_buffer.resize(total_pixels);

			auto* start = d_buffer.data();
			size_t size = d_width * d_height;
			for (size_t i = 0; i < size; ++i, start += gop) {
				d_data[i].last_pixels.set_buffer(start, gop);
			}

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

		const std::vector<PixelType>& pixels(size_t x, size_t y) const noexcept { return d_data[x + y * d_width].points; }
		const std::vector<PixelType>& pixels(size_t i) const noexcept { return d_data[i].points; }
		std::vector<PixelType>& pixels(size_t x, size_t y) noexcept { return d_data[x + y * d_width].points; }
		std::vector<PixelType>& pixels(size_t i) noexcept { return d_data[i].points; }

		std::string finish() { return finish_internal(); }

		virtual const std::vector<int64_t>& times() const noexcept { return d_times; }

		virtual std::string add_frame(const void* img, std::int64_t time) { return this->add_image(static_cast<const T*>(img), time); }

		virtual std::string add_frame_bytes(const void* img, int inner_stride_bytes, std::int64_t time) { return this->add_image_bytes(img, (size_t)inner_stride_bytes, time); }

		std::string add_image_bytes(const void* _img, size_t inner_bytes, int64_t time)
		{
			if (inner_bytes < sizeof(T))
				return {};

			static constexpr size_t alignment = alignof(T);
			bool is_aligned = ((uintptr_t)_img % alignment) == 0 && (inner_bytes % alignment) == 0 && (inner_bytes % sizeof(T) == 0);
			size_t inner = is_aligned ? inner_bytes / sizeof(T) : 0;
			const T* t_img = static_cast<const T*>(_img);
			const char* c_img = static_cast<const char*>(_img);

			if (d_pos == 0)
				d_times.clear();
			d_times.push_back(time);

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
				++d_pos;
				if (has_finish)
					return finish();
				return {};
			}

#endif
			if (d_max_gop == 0) {
				// initialize with 500MB
				set_max_memory(500000000u);
			}

			size_t size = d_width * d_height;

			if (d_pos == 0) {
				// #pragma omp parallel for num_threads(d_threads)
				get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto _i) {
					size_t pix = (size_t)_i;
					T val;
					if (is_aligned)
						val = t_img[pix * inner];
					else
						memcpy(&val, c_img + pix * inner_bytes, sizeof(T));
					PixelData& d = d_data[pix];
					d.candidate = MaxSpace;

					d.points.clear();
					d.last_pixels.clear();
					d.points.shrink_to_fit();
					d.last_pixels.shrink_to_fit();

					d.points.push_back(PixelType{ val, d_pos });
					d.last_pixels.push_back(d.points.back());
					d.pos = 1;
					d.start = 0;
				});
				++d_pos;
				return std::string();
			}

			// #pragma omp parallel for num_threads(d_threads)
			get_pool().loop_for(d_threads, 0, (int)size, 1, [&](auto p) {
				size_t pix = (size_t)p;
				PixelData& d = this->d_data[pix];

				T val;
				if (is_aligned)
					val = t_img[pix * inner];
				else
					memcpy(&val, c_img + pix * inner_bytes, sizeof(T));

				d.last_pixels.push_back(PixelType{ val, d_pos });
				advance_min_max(d, false);
				d.pos++;
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
		virtual bool read(void* img, size_t inner_stride) = 0;
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
		stenosv_block_header d_header;
		std::uint64_t d_pos{ 0 };
		std::vector<uint64_t> d_trace_pos;
		int d_threads{ 1 };

		std::vector<PixelType> d_time_traces;
		std::vector<std::pair<size_t, size_t>> d_poss;

		std::vector<PixelType> d_partial_time_traces;
		std::vector<std::pair<size_t, size_t>> d_partial_poss;

		bool open_skip_size(stenos_input* in, bool read_content = true)
		{
			if (!in)
				return false;

			in->read((char*)&d_header, sizeof(d_header), in->opaque);
			if (!in) {
				close();
				return false;
			}

			if (d_header.pixel_type != stenosv_to_pixel_type<T>())
				return false;

			d_times.resize(d_header.count);

			size_t size = (size_t)d_header.width * (size_t)d_header.height;

			// read times
			if (!compress_detail::read_compressed_vector(d_threads, in, d_times, d_header.count * 8)) {
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

			uint64_t pix_bytes = compress_detail::read_uint64(in);
			d_time_traces.clear();
			if (!compress_detail::read_compressed_vector(d_threads, in, d_time_traces, pix_bytes) || d_time_traces.size() < cnts_pix.size()) {
				close();
				return false;
			}
			d_poss.resize(cnts_pix.size());
			size_t cum_count = 0;
			for (size_t i = 0; i < cnts_pix.size(); ++i) {
				d_poss[i] = { cum_count, cnts_pix[i] };
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

			if (d_header.pixel_type != stenosv_to_pixel_type<T>()) {
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

			// Read ALL sizes
			std::vector<unsigned short> cnts_pix(size);
			if (!compress_detail::read_compressed_vector(d_threads, in, cnts_pix, size * 2)) {
				close();
				return res;
			}

			uint64_t pix_bytes = compress_detail::read_uint64(in);
			uint64_t csize = compress_detail::read_uint64(in);
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
		void set_threads(int threads) noexcept { d_threads = threads; }

		bool empty() const noexcept { return d_times.empty() || d_time_traces.empty(); }
		const stenosv_block_header& header() const noexcept { return d_header; }
		const std::vector<int64_t>& times() const noexcept { return d_times; }
		int64_t time() const noexcept { return d_times.empty() ? 0 : d_times[0]; }
		std::pair<int64_t, int64_t> bounds() const noexcept { return d_times.empty() ? std::pair<int64_t, int64_t>{ 0ll, 0ll } : std::pair<int64_t, int64_t>{ d_times[0], d_times.back() }; }
		TimeTraceRange time_trace(unsigned x, unsigned y) const
		{
			unsigned pos = x + y * d_header.width;
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
			uint64_t full_block_size = compress_detail::read_uint64(in);
			auto pos = in->tell(in->opaque);
			if (!in)
				return false;
			if (!open_skip_size(in, read_content))
				return false;
			if (!read_content) {
				in->seek(pos + (int64_t)full_block_size, STENOS_SEEK_SET, in->opaque);
				if (!in)
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

			uint64_t full_block_size = compress_detail::read_uint64(in);
			return open_skip_size_partial(in, coords, count, lock);
		}

		virtual bool seek_pos(uint64_t pos)
		{
			if (pos < 0 || pos >= (size_t)d_times.size())
				return false;
			if (pos == d_pos)
				return true;

			d_pos = pos;
			auto time = d_times[pos];

			size_t size = d_header.width * d_header.height;
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

		virtual bool read(void* img, size_t inner_stride = 1)
		{
			if (inner_stride == 1)
				return read_next<1>(static_cast<T*>(img), 1);
			else if (inner_stride == 2)
				return read_next<2>(static_cast<T*>(img), 2);
			else if (inner_stride == 3)
				return read_next<3>(static_cast<T*>(img), 3);
			else if (inner_stride == 4)
				return read_next<4>(static_cast<T*>(img), 4);
			else
				return read_next<0>(static_cast<T*>(img), inner_stride);
		}

		template<size_t InnerStride>
		static constexpr size_t get(size_t inner)
		{
			if constexpr (InnerStride > 0) {
				(void)inner;
				return InnerStride;
			}
			else {
				return inner;
			}
		}

		template<size_t InnerStride = 1>
		bool read_next(T* img, size_t inner)
		{
			if (d_pos >= d_times.size())
				return false;

			if (d_trace_pos.empty())
				d_trace_pos.resize(d_header.width * d_header.height, 0);

			int64_t utime = d_times[d_pos];
			double time = (double)d_times[d_pos];

			size_t size = d_header.width * d_header.height;
			size_t pixels_per_thread = size / (size_t)d_threads;
			size_t remaining = size % (size_t)d_threads;

			// #pragma omp parallel for num_threads(d_threads)
			get_pool().loop_for(d_threads, 0, d_threads, 1, [&](auto i) {
				size_t first = (size_t)i * pixels_per_thread;
				size_t end = first + ((i == d_threads - 1) ? (pixels_per_thread + remaining) : pixels_per_thread);
				size_t count = end - first;
				size_t end4 = first + (count & (~3ull));
				size_t j = first;
				for (; j < end4; j += 4) {
					const PixelType* tr1 = d_time_traces.data() + d_poss[j].first;
					const PixelType* tr2 = d_time_traces.data() + d_poss[j + 1].first;
					const PixelType* tr3 = d_time_traces.data() + d_poss[j + 2].first;
					const PixelType* tr4 = d_time_traces.data() + d_poss[j + 3].first;

					const size_t tr_size1 = d_poss[j].second;
					const size_t tr_size2 = d_poss[j + 1].second;
					const size_t tr_size3 = d_poss[j + 2].second;
					const size_t tr_size4 = d_poss[j + 3].second;

					const size_t pos1 = this->d_trace_pos[j];
					const size_t pos2 = this->d_trace_pos[j + 1];
					const size_t pos3 = this->d_trace_pos[j + 2];
					const size_t pos4 = this->d_trace_pos[j + 3];

					const auto prev_val1 = tr1[pos1];
					const auto prev_val2 = tr2[pos2];
					const auto prev_val3 = tr3[pos3];
					const auto prev_val4 = tr4[pos4];

					const auto next_val1 = (pos1 == tr_size1 - 1) ? tr1[pos1] : tr1[pos1 + 1];
					const auto next_val2 = (pos2 == tr_size2 - 1) ? tr2[pos2] : tr2[pos2 + 1];
					const auto next_val3 = (pos3 == tr_size3 - 1) ? tr3[pos3] : tr3[pos3 + 1];
					const auto next_val4 = (pos4 == tr_size4 - 1) ? tr4[pos4] : tr4[pos4 + 1];

					double value1 = (double)next_val1.value;
					double value2 = (double)next_val2.value;
					double value3 = (double)next_val3.value;
					double value4 = (double)next_val4.value;

					if (utime == d_times[next_val1.index])
						this->d_trace_pos[j] += (pos1 < tr_size1 - 1);
					else {
						double advance1 = (time - (double)d_times[prev_val1.index]) / ((double)d_times[next_val1.index] - (double)d_times[prev_val1.index]);
						value1 = value1 * advance1 + (1. - advance1) * prev_val1.value;
					}
					if (utime == d_times[next_val2.index])
						this->d_trace_pos[j + 1] += (pos2 < tr_size2 - 1);
					else {
						double advance2 = (time - (double)d_times[prev_val2.index]) / ((double)d_times[next_val2.index] - (double)d_times[prev_val2.index]);
						value2 = value2 * advance2 + (1. - advance2) * prev_val2.value;
					}
					if (utime == d_times[next_val3.index])
						this->d_trace_pos[j + 2] += (pos3 < tr_size3 - 1);
					else {
						double advance3 = (time - (double)d_times[prev_val3.index]) / ((double)d_times[next_val3.index] - (double)d_times[prev_val3.index]);
						value3 = value3 * advance3 + (1. - advance3) * prev_val3.value;
					}
					if (utime == d_times[next_val4.index])
						this->d_trace_pos[j + 3] += (pos4 < tr_size4 - 1);
					else {
						double advance4 = (time - (double)d_times[prev_val4.index]) / ((double)d_times[next_val4.index] - (double)d_times[prev_val4.index]);
						value4 = value4 * advance4 + (1. - advance4) * prev_val4.value;
					}

					img[j * get<InnerStride>(inner)] = compress_detail::fround_to<T>(value1);
					img[(j + 1) * get<InnerStride>(inner)] = compress_detail::fround_to<T>(value2);
					img[(j + 2) * get<InnerStride>(inner)] = compress_detail::fround_to<T>(value3);
					img[(j + 3) * get<InnerStride>(inner)] = compress_detail::fround_to<T>(value4);
				}

				for (; j < end; ++j) {
					const PixelType* tr = d_time_traces.data() + d_poss[j].first;
					const size_t tr_size = d_poss[j].second;
					const size_t pos = this->d_trace_pos[j];
					const auto prev_val = tr[pos];
					const auto next_val = (pos == tr_size - 1) ? tr[pos] : tr[pos + 1];

					double value = (double)next_val.value;
					if (utime == d_times[next_val.index]) {
						this->d_trace_pos[j] += (pos < tr_size - 1);
					}
					else {
						double advance = (time - (double)d_times[prev_val.index]) / ((double)d_times[next_val.index] - (double)d_times[prev_val.index]);
						value = value * advance + (1. - advance) * prev_val.value;
					}
					img[j * get<InnerStride>(inner)] = compress_detail::fround_to<T>(value);
				}
			});

			++d_pos;
			return true;
		}

		bool read_bytes(void* img, size_t inner_bytes)
		{
			if (d_pos >= d_times.size())
				return false;

			if (inner_bytes < sizeof(T))
				return false;

			if (d_trace_pos.empty())
				d_trace_pos.resize(d_header.width * d_header.height, 0);

			int64_t utime = d_times[d_pos];
			double time = (double)d_times[d_pos];

			size_t size = d_header.width * d_header.height;
			size_t pixels_per_thread = size / (size_t)d_threads;
			size_t remaining = size % (size_t)d_threads;

			static constexpr size_t alignment = alignof(T);
			bool is_aligned = ((uintptr_t)img % alignment) == 0 && (inner_bytes % alignment) == 0 && (inner_bytes % sizeof(T) == 0);
			size_t inner = is_aligned ? inner_bytes / sizeof(T) : 0;
			T* t_img = static_cast<T*>(img);
			char* c_img = static_cast<char*>(img);

			// #pragma omp parallel for num_threads(d_threads)
			get_pool().loop_for(d_threads, 0, d_threads, 1, [&](auto i) {
				size_t first = (size_t)i * pixels_per_thread;
				size_t end = first + ((i == d_threads - 1) ? (pixels_per_thread + remaining) : pixels_per_thread);
				size_t count = end - first;
				size_t end4 = first + (count & (~3ull));
				size_t j = first;
				for (; j < end4; j += 4) {
					const PixelType* tr1 = d_time_traces.data() + d_poss[j].first;
					const PixelType* tr2 = d_time_traces.data() + d_poss[j + 1].first;
					const PixelType* tr3 = d_time_traces.data() + d_poss[j + 2].first;
					const PixelType* tr4 = d_time_traces.data() + d_poss[j + 3].first;

					const size_t tr_size1 = d_poss[j].second;
					const size_t tr_size2 = d_poss[j + 1].second;
					const size_t tr_size3 = d_poss[j + 2].second;
					const size_t tr_size4 = d_poss[j + 3].second;

					const size_t pos1 = this->d_trace_pos[j];
					const size_t pos2 = this->d_trace_pos[j + 1];
					const size_t pos3 = this->d_trace_pos[j + 2];
					const size_t pos4 = this->d_trace_pos[j + 3];

					const auto prev_val1 = tr1[pos1];
					const auto prev_val2 = tr2[pos2];
					const auto prev_val3 = tr3[pos3];
					const auto prev_val4 = tr4[pos4];

					const auto next_val1 = (pos1 == tr_size1 - 1) ? tr1[pos1] : tr1[pos1 + 1];
					const auto next_val2 = (pos2 == tr_size2 - 1) ? tr2[pos2] : tr2[pos2 + 1];
					const auto next_val3 = (pos3 == tr_size3 - 1) ? tr3[pos3] : tr3[pos3 + 1];
					const auto next_val4 = (pos4 == tr_size4 - 1) ? tr4[pos4] : tr4[pos4 + 1];

					double value1 = (double)next_val1.value;
					double value2 = (double)next_val2.value;
					double value3 = (double)next_val3.value;
					double value4 = (double)next_val4.value;

					if (utime == d_times[next_val1.index])
						this->d_trace_pos[j] += (pos1 < tr_size1 - 1);
					else {
						double advance1 = (time - (double)d_times[prev_val1.index]) / ((double)d_times[next_val1.index] - (double)d_times[prev_val1.index]);
						value1 = value1 * advance1 + (1. - advance1) * prev_val1.value;
					}
					if (utime == d_times[next_val2.index])
						this->d_trace_pos[j + 1] += (pos2 < tr_size2 - 1);
					else {
						double advance2 = (time - (double)d_times[prev_val2.index]) / ((double)d_times[next_val2.index] - (double)d_times[prev_val2.index]);
						value2 = value2 * advance2 + (1. - advance2) * prev_val2.value;
					}
					if (utime == d_times[next_val3.index])
						this->d_trace_pos[j + 2] += (pos3 < tr_size3 - 1);
					else {
						double advance3 = (time - (double)d_times[prev_val3.index]) / ((double)d_times[next_val3.index] - (double)d_times[prev_val3.index]);
						value3 = value3 * advance3 + (1. - advance3) * prev_val3.value;
					}
					if (utime == d_times[next_val4.index])
						this->d_trace_pos[j + 3] += (pos4 < tr_size4 - 1);
					else {
						double advance4 = (time - (double)d_times[prev_val4.index]) / ((double)d_times[next_val4.index] - (double)d_times[prev_val4.index]);
						value4 = value4 * advance4 + (1. - advance4) * prev_val4.value;
					}

					if (is_aligned) {

						t_img[j * inner] = compress_detail::fround_to<T>(value1);
						t_img[(j + 1) * inner] = compress_detail::fround_to<T>(value2);
						t_img[(j + 2) * inner] = compress_detail::fround_to<T>(value3);
						t_img[(j + 3) * inner] = compress_detail::fround_to<T>(value4);
					}
					else {
						T tmp1 = compress_detail::fround_to<T>(value1);
						T tmp2 = compress_detail::fround_to<T>(value2);
						T tmp3 = compress_detail::fround_to<T>(value3);
						T tmp4 = compress_detail::fround_to<T>(value4);
						memcpy(c_img + j * inner_bytes, &tmp1, sizeof(T));
						memcpy(c_img + (j + 1) * inner_bytes, &tmp2, sizeof(T));
						memcpy(c_img + (j + 2) * inner_bytes, &tmp3, sizeof(T));
						memcpy(c_img + (j + 3) * inner_bytes, &tmp4, sizeof(T));
					}
				}

				for (; j < end; ++j) {
					const PixelType* tr = d_time_traces.data() + d_poss[j].first;
					const size_t tr_size = d_poss[j].second;
					const size_t pos = this->d_trace_pos[j];
					const auto prev_val = tr[pos];
					const auto next_val = (pos == tr_size - 1) ? tr[pos] : tr[pos + 1];

					double value = (double)next_val.value;
					if (utime == d_times[next_val.index]) {
						this->d_trace_pos[j] += (pos < tr_size - 1);
					}
					else {
						double advance = (time - (double)d_times[prev_val.index]) / ((double)d_times[next_val.index] - (double)d_times[prev_val.index]);
						value = value * advance + (1. - advance) * prev_val.value;
					}
					if (is_aligned)
						t_img[j * inner] = compress_detail::fround_to<T>(value);
					else {
						T tmp = compress_detail::fround_to<T>(value);
						memcpy(c_img + j * inner_bytes, &tmp, sizeof(T));
					}
				}
			});

			++d_pos;
			return true;
		}
	};

} // end namespace stenos
