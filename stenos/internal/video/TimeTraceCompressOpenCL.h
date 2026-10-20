#ifndef STENOS_TIME_TRACE_COMPRESS_OPENCL_H
#define STENOS_TIME_TRACE_COMPRESS_OPENCL_H

// Target OpenCL 1.2, that's enough for us
#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_TARGET_OPENCL_VERSION 120

#include <utility>
#include <type_traits>
#include <iterator>
#include <algorithm>
#include <vector>
#include <memory>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include "../../bits.hpp"
#include "CL/opencl.hpp"

// Load the kernel at compile time to avoid shipping an additional file
static const char* kernel_code =
#include "time_trace_kernel.cl"
  ;

namespace stenos
{

	struct GPUDevice
	{
		std::string name;
		uint64_t compute_units;
		uint64_t max_memory;
		uint64_t vendor_id;
		cl::Device device;
	};

	static inline std::vector<GPUDevice> computeSupportedGPUs()
	{
		std::vector<GPUDevice> ret;
		std::vector<cl::Platform> platforms;
		cl::Platform::get(&platforms);
		cl::Platform plat;
		for (auto& p : platforms) {
			std::vector<cl::Device> devices;
			p.getDevices(CL_DEVICE_TYPE_GPU, &devices);
			for (auto& d : devices) {
				auto fp = d.getInfo<CL_DEVICE_DOUBLE_FP_CONFIG>();
				if (fp != 0) {
					GPUDevice g;
					g.name = d.getInfo<CL_DEVICE_NAME>();
					g.compute_units = d.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
					g.max_memory = d.getInfo<CL_DEVICE_MAX_MEM_ALLOC_SIZE>();
					g.vendor_id = d.getInfo<CL_DEVICE_VENDOR_ID>();
					g.device = d;
					// For now ignore Intel devices as most of them produce buggy results
					if (g.vendor_id != 0x8086)
						ret.push_back(std::move(g));
				}
			}
		}
		return ret;
	}
	inline const std::vector<GPUDevice>& getSupportedGPUs()
	{
		static std::vector<GPUDevice> ret = computeSupportedGPUs();
		return ret;
	}

	static inline int computeDefaultGPU()
	{
		int found = -1;
		const auto& gpus = getSupportedGPUs();
		for (int i = 0; i < (int)gpus.size(); ++i) {
			if (found < 0)
				found = i;
			else {
				const auto& g = gpus[(size_t)i];
				const auto& prev = gpus[(size_t)found];
				if (g.compute_units > prev.compute_units) {
					found = i;
				}
			}
		}
		return found;
	}
	inline int getDefaultGPU()
	{
		static int ret = computeDefaultGPU();
		return ret;
	}

#pragma pack(push, 1)
	// Pixel value and temporal position, similar to the one in TimeTraceCompressionFast.h
	// and within time_trace_kernel.cl
	template<class T>
	struct CLPixelType
	{
		T value;
		unsigned short index;
	};
#pragma pack(pop)

	// Fixed-width device buffer layout. buffer/data are element offsets into
	// d_buffer_pixels, never pointers copied between host and device.
	template<class T>
	struct alignas(8) CL_FIFO
	{
		cl_ulong buffer;
		cl_ulong data;
		cl_int size;
		cl_int reserved;
	};

	template<class T>
	struct alignas(8) CLPixelData
	{
		CL_FIFO<T> points;
		CL_FIFO<T> last_pixels;
		cl_int candidate;
		cl_int pos;
		cl_int finished;
		cl_int reserved;
		cl_double max_slope;
		cl_double min_slope;
	};

	/// Generate 2 things:
	/// -	the pixel type itself used by the OpenCL kernel
	/// -	the kernel name suffix
	template<class T>
	constexpr auto generateTypeName()
	{
		static_assert(std::is_arithmetic_v<T>);
		if constexpr (sizeof(T) == 1) {
			if constexpr (std::is_signed_v<T>)
				return std::make_pair((char)0, "char");
			else
				return std::make_pair((unsigned char)0, "uchar");
		}
		else if constexpr (sizeof(T) == 2) {
			if constexpr (std::is_signed_v<T>)
				return std::make_pair((short)0, "short");
			else
				return std::make_pair((unsigned short)0, "ushort");
		}
		else if constexpr (sizeof(T) == 4 && std::is_integral_v<T>) {
			if constexpr (std::is_signed_v<T>)
				return std::make_pair((int)0, "int");
			else
				return std::make_pair((unsigned int)0, "uint");
		}
		else if constexpr (sizeof(T) == 8 && std::is_integral_v<T>) {
			if constexpr (std::is_signed_v<T>)
				return std::make_pair((int64_t)0, "long");
			else
				return std::make_pair((uint64_t)0, "ulong");
		}
		else if constexpr (std::is_same_v<T, float>)
			return std::make_pair((float)0, "float");
		else
			return std::make_pair((double)0, "double");
	}

	/// @brief OpenCL implementation of the video time trace compression algorithm.
	/// The CPU based implementation is available in TimeTraceCompressFast.h.
	/// Uses best-effort slope bounds and preserves non-finite samples as run
	/// boundaries. Retrieve traces after finish_block() and before adding the
	/// next GOP. The kernel and header must be updated together.
	template<class T>
	class TimeTraceCompressCL
	{
		// Retrieve result time traces by this number of lines
		// to avoid allocating GB of data.
		static constexpr size_t retrieve_line_count = 16;

		using PixelType = CLPixelType<T>;

		size_t d_width{ 0 };
		size_t d_height{ 0 };
		size_t d_gop{ 100 };
		double d_error{ 0 };
		double d_active_error{ 0 };
		bool d_ready{ false };
		int d_pos{ 0 };

		std::string d_name = generateTypeName<T>().second;

		std::vector<int64_t> d_times;
		std::vector<CLPixelData<T>> d_data;

		cl::Device d_device;
		cl::Context d_context;
		cl::Program d_program;
		cl::CommandQueue d_queue;
		cl::Buffer d_buffer_pixels;
		cl::Buffer d_buffer_data;
		cl::Buffer d_buffer_times;
		cl::Buffer d_buffer_img;
		cl::Event d_last_evt;
		std::vector<PixelType> d_trace;
		size_t d_trace_h = std::numeric_limits<size_t>::max();

		using advance_pixel_data_type = cl::KernelFunctor<cl::Buffer, cl::Buffer, cl::Buffer, int, double, int, cl::Buffer>;
		std::unique_ptr<advance_pixel_data_type> d_advance_pixel_data;

		using insert_key_frame_type = cl::KernelFunctor<cl::Buffer, cl::Buffer, int, double, cl::Buffer>;
		std::unique_ptr<insert_key_frame_type> d_insert_key_frame;

		static size_t checked_product(size_t a, size_t b)
		{
			if (b && a > std::numeric_limits<size_t>::max() / b)
				throw std::length_error("TimeTraceCompressCL buffer size overflow");
			return a * b;
		}

		void check_retrieval(size_t idx) const
		{
			if (!d_ready) throw std::logic_error("Finish the GOP before retrieving traces");
			if (idx >= d_width * d_height) throw std::out_of_range("Invalid pixel index");
		}

		void flush_runs()
		{
			cl::EnqueueArgs args(d_queue, cl::NullRange, cl::NDRange(d_width * d_height), cl::NullRange);
			(*d_insert_key_frame)(args, d_buffer_data, d_buffer_pixels, d_pos, d_active_error, d_buffer_times);
		}

		PixelType* bufferize_time_trace(size_t idx)
		{
			check_retrieval(idx);
			const size_t h = idx / d_width;
			const size_t w = idx % d_width;
			const size_t start_line = (h / retrieve_line_count) * retrieve_line_count;
			if (d_trace_h != start_line) {
				const size_t lines = std::min(retrieve_line_count, d_height - start_line);
				d_queue.enqueueReadBuffer(d_buffer_pixels, CL_TRUE,
					sizeof(PixelType) * d_width * start_line * d_gop * 2,
					sizeof(PixelType) * d_width * d_gop * 2 * lines, d_trace.data());
				d_trace_h = start_line;
			}
			return d_trace.data() + (w + (h - start_line) * d_width) * d_gop * 2;
		}

	public:
		static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool> &&
			(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8), "invalid pixel type");
		static_assert(sizeof(PixelType) == sizeof(T) + 2, "Pixel packing mismatch");
		static_assert(sizeof(CL_FIFO<T>) == 24 && sizeof(CLPixelData<T>) == 80, "OpenCL state layout mismatch");
		static_assert(offsetof(CLPixelData<T>, max_slope) == 64, "OpenCL slope alignment mismatch");

		TimeTraceCompressCL(int device_idx, size_t width, size_t height, double error, size_t GOP)
		{
			if (!width || !height || !std::isfinite(error)) return;
			try {
				if (device_idx < 0 || device_idx >= (int)getSupportedGPUs().size()) return;
				d_device = getSupportedGPUs()[(size_t)device_idx].device;
				// The packed sample buffers and state buffers use native byte order.
				const uint16_t one = 1;
				const bool host_little = *reinterpret_cast<const unsigned char*>(&one) != 0;
				if ((d_device.getInfo<CL_DEVICE_ENDIAN_LITTLE>() != 0) != host_little) return;
				d_context = cl::Context(d_device);
				cl::Program::Sources sources;
				sources.push_back(kernel_code);
				d_program = cl::Program(d_context, sources);
				// Finite-math/fast-relaxed-math would invalidate non-finite handling.
				if (d_program.build(d_device, "-cl-std=CL1.2") != CL_SUCCESS) return;

				d_width = width;
				d_height = height;
				d_error = std::fabs(error);
				d_active_error = d_error;
				// Preserve the existing public GOP policy.
				d_gop = std::max<size_t>(4, std::min<size_t>(512, GOP));
				const size_t pixel_count = checked_product(d_width, d_height);
				const size_t trace_bytes = checked_product(
					checked_product(pixel_count, 2 * d_gop), sizeof(PixelType));
				d_queue = cl::CommandQueue(d_context, d_device); // In-order queue.
				d_buffer_times = cl::Buffer(d_context, CL_MEM_READ_ONLY, sizeof(cl_long) * d_gop);
				d_buffer_pixels = cl::Buffer(d_context, CL_MEM_READ_WRITE, trace_bytes);
				d_buffer_data = cl::Buffer(d_context, CL_MEM_READ_WRITE,
					checked_product(sizeof(CLPixelData<T>), pixel_count));
				d_buffer_img = cl::Buffer(d_context, CL_MEM_READ_ONLY, checked_product(sizeof(T), pixel_count));
				d_advance_pixel_data.reset(new advance_pixel_data_type(d_program, "advance_pixel_data_" + d_name));
				d_insert_key_frame.reset(new insert_key_frame_type(d_program, "insert_key_frame_" + d_name));
				d_times.reserve(d_gop);
				d_data.resize(pixel_count);
				d_trace.resize(checked_product(checked_product(2 * d_gop, d_width),
					std::min(d_height, retrieve_line_count)));
			}
			catch (...) {
				d_width = d_height = 0;
			}
		}

		TimeTraceCompressCL(const TimeTraceCompressCL&) = delete;
		TimeTraceCompressCL& operator=(const TimeTraceCompressCL&) = delete;
		~TimeTraceCompressCL() noexcept {}

		size_t gop() const noexcept { return d_gop; }
		size_t width() const noexcept { return d_width; }
		size_t height() const noexcept { return d_height; }
		bool is_valid() const noexcept { return d_width > 0; }

		// Applied at the next frame. A changed error closes current finite runs
		// under the previous bound; it does not retroactively alter old segments.
		void set_error(double error) noexcept { d_error = std::fabs(error); }
		double error() const noexcept { return d_error; }

		void finish_block()
		{
			if (d_pos == 0) return;
			flush_runs();
			d_queue.enqueueReadBuffer(d_buffer_data, CL_TRUE, 0,
				sizeof(CLPixelData<T>) * d_width * d_height, d_data.data());
			d_pos = 0;
			d_ready = true;
			d_trace_h = std::numeric_limits<size_t>::max();
		}

		// time is an int64 nanosecond timestamp; it must increase within a GOP.
		// Uploads complete before return, so img may immediately be reused. The
		// decimation kernel remains asynchronous until the next frame or finish.
		bool add_image(const T* img, int64_t time)
		{
			if (!is_valid()) throw std::logic_error("Invalid OpenCL compressor");
			if (!img || !std::isfinite(d_error)) throw std::invalid_argument("Invalid image or error");
			if (d_pos != 0 && time <= d_times.back())
				throw std::invalid_argument("Timestamps must strictly increase within a GOP");
			if (d_last_evt.get()) d_last_evt.wait();

			if (d_pos == 0) {
				d_times.clear();
				d_active_error = d_error;
				d_ready = false;
				d_trace_h = std::numeric_limits<size_t>::max();
			} else if (d_error != d_active_error) {
				flush_runs();
				d_active_error = d_error;
			}
			const cl_long timestamp = static_cast<cl_long>(time);
			d_queue.enqueueWriteBuffer(d_buffer_times, CL_TRUE,
				sizeof(cl_long) * d_pos, sizeof(timestamp), &timestamp);
			d_queue.enqueueWriteBuffer(d_buffer_img, CL_TRUE, 0,
				sizeof(T) * d_width * d_height, img);
			cl::EnqueueArgs args(d_queue, cl::NullRange, cl::NDRange(d_width * d_height), cl::NullRange);
			d_last_evt = (*d_advance_pixel_data)(args, d_buffer_data, d_buffer_pixels,
				d_buffer_img, d_pos, d_active_error, (int)d_gop, d_buffer_times);
			d_times.push_back(time);
			++d_pos;
			if (d_pos >= (int)d_gop) {
				finish_block();
				return true;
			}
			return false;
		}

		size_t decimated_pixel_count(size_t idx) const
		{
			check_retrieval(idx);
			auto* data = d_data.data() + idx;
			return (size_t)data->points.size;
		}

		size_t retrieve_decimated_pixels(size_t idx, std::vector<PixelType>& out)
		{
			check_retrieval(idx);
			auto* data = d_data.data() + idx;
			auto* trace = bufferize_time_trace(idx) + d_gop;
			trace += (size_t)(data->points.data - data->points.buffer);

			for (size_t i = 0; i < (size_t)data->points.size; ++i)
				out.push_back(trace[i]);

			return (size_t)data->points.size;
		}

		size_t retrieve_raw_pixels(size_t idx, std::vector<PixelType>& out)
		{
			auto* trace = bufferize_time_trace(idx);

			for (size_t i = 0; i < d_times.size(); ++i) {
				out.push_back(trace[i]);
			}
			return d_times.size();
		}
	};
}

#endif
