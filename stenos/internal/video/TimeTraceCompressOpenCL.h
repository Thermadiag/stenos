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
				if (fp & cl_khr_fp64) {
					GPUDevice g;
					g.name = d.getInfo<CL_DEVICE_NAME>();
					g.compute_units = d.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
					g.max_memory = d.getInfo<CL_DEVICE_MAX_MEM_ALLOC_SIZE>();
					g.vendor_id = d.getInfo<CL_DEVICE_VENDOR_ID>();
					g.device = d;
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

	/* struct TimeTraceContext
	{
		int device_id = -1;
		cl::Device device;
		cl::Context context;
		cl::Program program;
		std::string error;
	};

	// Returns all opencl
	static inline TimeTraceContext* getContext(int device)
	{
		static std::deque<TimeTraceContext> inst;
		static std::mutex mutex;

		if (device < 0 || device >= (int)getSupportedGPUs().size())
			return nullptr;

		std::scoped_lock<std::mutex> lock(mutex);

		for (TimeTraceContext& c : inst) {
			if (c.device_id == device)
				return &c;
		}
		TimeTraceContext& c = inst.emplace_back();
		c.device = (getSupportedGPUs()[(size_t)device].device);
		c.context = cl::Context(c.device);
		cl::Program::Sources sources;
		sources.push_back(kernel_code);
		c.program = cl::Program(c.context, sources);
		if (c.program.build(c.device) != CL_SUCCESS)
			c.error = c.program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(c.device);
		return &c;
	}*/

#pragma pack(1)
	// Pixel value and temporal position, similar to the one in TimeTraceCompressionFast.h
	// and within time_trace_kernel.cl
	template<class T>
	struct CLPixelType
	{
		T value;
		unsigned short index;
	};
#pragma pack()

	// Mimic structures used in time_trace_kernel.cl
	// for memory exchange purpose
	template<class T>
	struct CL_FIFO
	{
		CLPixelType<T>* buffer;
		CLPixelType<T>* data;
		int size;
	};

	template<class T>
	struct CLPixelData
	{
		CL_FIFO<T> points;
		CL_FIFO<T> last_pixels;
		int candidate;
		int pos;
		int start;
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
	/// The CPU based implementation is available in TimeTraceCompressFast.h
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
		size_t d_trace_h = ((size_t)-1) - retrieve_line_count;

		using advance_pixel_data_type = cl::KernelFunctor<cl::Buffer, cl::Buffer, cl::Buffer, int, double, int, cl::Buffer>;
		std::unique_ptr<advance_pixel_data_type> d_advance_pixel_data;

		using insert_key_frame_type = cl::KernelFunctor<cl::Buffer, int, double, cl::Buffer>;
		std::unique_ptr<insert_key_frame_type> d_insert_key_frame;

		PixelType* bufferize_time_trace(size_t idx)
		{
			// We need to grab the decimated time trace for given pixel idx
			size_t h = idx / d_width;
			size_t w = idx % d_width;
			size_t start_line = (h / retrieve_line_count) * retrieve_line_count;

			if (h < d_trace_h || h >= d_trace_h + retrieve_line_count) {

				size_t lines = retrieve_line_count;
				if (start_line + lines > d_height)
					lines = d_height - start_line;

				d_queue.enqueueReadBuffer(
				  d_buffer_pixels, CL_TRUE, sizeof(PixelType) * d_width * start_line * d_gop * 2, sizeof(PixelType) * d_width * d_gop * 2 * lines, d_trace.data());
				d_trace_h = h;
			}

			return d_trace.data() + w * d_gop * 2 + (h - start_line) * d_width * d_gop * 2;
		}

	public:
		static_assert(std::is_arithmetic<T>::value, "invalid pixel type");

		TimeTraceCompressCL(int device_idx, size_t width, size_t height, double error, size_t GOP)
		{
			if (device_idx < 0 || device_idx >= (int)getSupportedGPUs().size())
				return;

			d_device = (getSupportedGPUs()[(size_t)device_idx].device);
			d_context = cl::Context(d_device);
			cl::Program::Sources sources;
			sources.push_back(kernel_code);

			d_program = cl::Program(d_context, sources);
			if (d_program.build(d_device) != CL_SUCCESS) {
				// RIR_LOG_ERROR("error building opencl kernel: ", d_program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(d_device));
				// std::cout << d_program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(d_device) << std::endl;
				return;
			}

			d_width = width;
			d_height = height;
			d_error = fabs(error);
			d_pos = 0;
			d_gop = GOP;
			d_times.clear();

			if (d_gop < 4)
				d_gop = 4;
			else if (d_gop > 512)
				d_gop = 512;

			try {

				// Build OCL command queue
				d_queue = cl::CommandQueue(d_context, d_device);

				// Allocate memory on device
				d_buffer_times = cl::Buffer(d_context, CL_MEM_READ_WRITE, sizeof(double) * GOP);
				d_buffer_pixels = cl::Buffer(d_context, CL_MEM_READ_WRITE, width * height * 2 * GOP * sizeof(PixelType));
				d_buffer_data = cl::Buffer(d_context, CL_MEM_READ_WRITE, sizeof(CLPixelData<T>) * width * height);
				d_buffer_img = cl::Buffer(d_context, CL_MEM_READ_WRITE, d_width * d_height * sizeof(T));

				// Retrieve kernel functions
				d_advance_pixel_data.reset(new advance_pixel_data_type(d_program, "advance_pixel_data_" + d_name));
				d_insert_key_frame.reset(new insert_key_frame_type(d_program, "insert_key_frame_" + d_name));

				// Internal buffers
				d_data.resize(d_width * d_height);
				d_trace.resize(d_gop * 2 * d_width * retrieve_line_count);
			}
			catch (...) {
				// RIR_LOG_ERROR("error while initializing opencl context and buffers");
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

		void set_error(double error) noexcept { d_error = error; }
		double error() const noexcept { return d_error; }

		void finish_block()
		{
			// Finish a block of images (or GOP)

			if (d_pos == 0)
				return;

			// Launch kernel
			cl::EnqueueArgs args(d_queue, cl::NullRange, cl::NDRange(d_width * d_height), cl::NullRange);
			(*d_insert_key_frame)(args, d_buffer_data, d_pos, d_error, d_buffer_times);

			// We need to retrieve the data buffer
			d_queue.enqueueReadBuffer(d_buffer_data, CL_TRUE, 0, sizeof(CLPixelData<T>) * d_width * d_height, d_data.data());
			d_pos = 0;
		}

		bool add_image(const T* img, int64_t time)
		{
			if (d_pos == 0)
				d_times.clear();
			d_times.push_back(time);

			double ftime = ((double)time * 1e-3); // convert to ms

			// Wait for previous image to finish (if any)
			if (d_last_evt.get())
				d_last_evt.wait();

			// Write timestamp to the device
			d_queue.enqueueWriteBuffer(d_buffer_times, CL_FALSE, sizeof(double) * d_pos, sizeof(double), &ftime);
			// Write image to the device
			d_queue.enqueueWriteBuffer(d_buffer_img, CL_FALSE, 0, sizeof(T) * d_width * d_height, img);

			// Launch kernel without waiting
			cl::EnqueueArgs args(d_queue, cl::NullRange, cl::NDRange(d_width * d_height), cl::NullRange);
			d_last_evt = (*d_advance_pixel_data)(args, d_buffer_data, d_buffer_pixels, d_buffer_img, d_pos, d_error, (int)d_gop, d_buffer_times);

			++d_pos;
			if (d_pos >= d_gop) {
				finish_block();
				return true;
			}
			return false;
		}

		size_t decimated_pixel_count(size_t idx) const
		{
			auto* data = d_data.data() + idx;
			return (size_t)data->points.size;
		}

		size_t retrieve_decimated_pixels(size_t idx, std::vector<PixelType>& out)
		{
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
			return (size_t)d_gop;
		}
	};
}

#endif