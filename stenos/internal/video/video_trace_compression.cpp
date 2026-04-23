#include <memory>

#include "../../stenos_video.h"
#include "TimeTraceCompressFast.h"

using namespace stenos;

stenos_gpu_device* stenos_list_gpu_devices(int* count)
{
	static std::vector<stenos_gpu_device> devices;

#ifdef STENOS_OPENCL
	if (devices.empty() && !getSupportedGPUs().empty()) {
		const auto& gpus = getSupportedGPUs();
		devices.resize(gpus.size());
		for (size_t i = 0; i < gpus.size(); ++i) {
			auto& d = devices[i];
			d.name = gpus[i].name.c_str();
			d.compute_units = gpus[i].compute_units;
			d.max_memory = gpus[i].max_memory;
			d.vendor_id = gpus[i].vendor_id;
		}
	}
#endif
	*count = (int)devices.size();
	return devices.empty() ? nullptr : devices.data();
}

int stenos_default_gpu_device()
{
#ifdef STENOS_OPENCL
	static int default_device = getDefaultGPU();
#else
	static int default_device = -1;
#endif
	return default_device;
}

struct stenos_vcompress_s
{
	std::unique_ptr<BaseTimeTraceCompress> compress;
	stenos_pixel_type pixel_type;
	std::string payload;
};

stenos_vcompress* stenos_vcompress_make(stenos_pixel_type type, int width, int height, int GOP, int device)
{
	stenos_vcompress* ret = nullptr;

	try {
		ret = new stenos_vcompress();
		ret->pixel_type = type;
		switch (type) {
			case StenosInt8:
				ret->compress.reset(new TimeTraceCompressFast<int8_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosUInt8:
				ret->compress.reset(new TimeTraceCompressFast<uint8_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosInt16:
				ret->compress.reset(new TimeTraceCompressFast<int16_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosUInt16:
				ret->compress.reset(new TimeTraceCompressFast<uint16_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosInt32:
				ret->compress.reset(new TimeTraceCompressFast<int32_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosUInt32:
				ret->compress.reset(new TimeTraceCompressFast<uint32_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosInt64:
				ret->compress.reset(new TimeTraceCompressFast<int64_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosUInt64:
				ret->compress.reset(new TimeTraceCompressFast<uint64_t>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosFloat32:
				ret->compress.reset(new TimeTraceCompressFast<float>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			case StenosFloat64:
				ret->compress.reset(new TimeTraceCompressFast<double>((size_t)width, (size_t)height, 0., (size_t)GOP, 1, device));
				break;
			default:
				delete ret;
				ret = nullptr;
				break;
		}
	}
	catch (...) {
	}
	return ret;
}

void stenos_vcompress_destroy(stenos_vcompress* ctx)
{
	if (ctx)
		delete ctx;
}

void stenos_vcompress_set_threads(stenos_vcompress* ctx, int threads)
{
	if (threads < 1)
		threads = 1;
	ctx->compress->set_threads(threads);
}
void stenos_vcompress_set_clevel(stenos_vcompress* ctx, int level)
{
	ctx->compress->set_compression_level(level);
}
void stenos_vcompress_set_max_error(stenos_vcompress* ctx, double error)
{
	ctx->compress->set_error(error);
}

size_t stenos_vcompress_add_image(stenos_vcompress* ctx, void* img, int64_t timestamp)
{
	const auto &times = ctx->compress->times();
	if (!times.empty() && timestamp <= times.back())
		return STENOS_ERROR_INVALID_PARAMETER;

	ctx->payload.clear();
	ctx->payload = ctx->compress->add_frame(img, timestamp);
	if (ctx->payload.empty())
		return 0;
	return 1;
}

size_t stenos_vcompress_stop(stenos_vcompress* ctx)
{
	ctx->payload = ctx->compress->finish();
	return 0;
}

stenos_payload stenos_vcompress_payload(stenos_vcompress* ctx)
{
	if (ctx->payload.empty())
		return { nullptr, 0 };
	else
		return { ctx->payload.data(), (uint64_t)ctx->payload.size() };
}

struct InputBuffer
{
	stenos_payload buf{ nullptr, 0 };
	int64_t pos = 0;
};
static int64_t read_stream(char* dst, int64_t size, void* opaque)
{
	InputBuffer* iss = static_cast<InputBuffer*>(opaque);
	int64_t rem = (int64_t)iss->buf.size - iss->pos;
	size = std::min(size, rem);
	if (size) {
		memcpy(dst, (char*)iss->buf.data + iss->pos, size);
		iss->pos += size;
	}
	return size;
}
static int64_t seek_stream(int64_t pos, int whence, void* opaque)
{
	InputBuffer* iss = static_cast<InputBuffer*>(opaque);
	if (whence == STENOS_SEEK_SET) {
		if (pos < 0)
			return (int64_t)STENOS_ERROR_INVALID_IO;
		iss->pos = (int64_t)pos;
	}
	else {
		iss->pos += pos;
		if (iss->pos < 0)
			return (int64_t)STENOS_ERROR_INVALID_IO;
	}
	return 0;
}
static int64_t tell_stream(void* opaque)
{
	return static_cast<InputBuffer*>(opaque)->pos;
}

struct stenos_vdecompress_s
{
	std::unique_ptr<BaseTimeTraceDecompressBlock> dec;
	stenos_input input;
	stenos_pixel_type pixel_type;
	InputBuffer buffer;
};

static BaseTimeTraceDecompressBlock* from_type(stenos_pixel_type type)
{
	BaseTimeTraceDecompressBlock* ret = nullptr;
	try {
		switch (type) {
			case StenosInt8:
				ret = new TimeTraceDecompressBlock<int8_t>();
				break;
			case StenosUInt8:
				ret = new TimeTraceDecompressBlock<uint8_t>();
				break;
			case StenosInt16:
				ret = new TimeTraceDecompressBlock<int16_t>();
				break;
			case StenosUInt16:
				ret = new TimeTraceDecompressBlock<uint16_t>();
				break;
			case StenosInt32:
				ret = new TimeTraceDecompressBlock<int32_t>();
				break;
			case StenosUInt32:
				ret = new TimeTraceDecompressBlock<uint32_t>();
				break;
			case StenosInt64:
				ret = new TimeTraceDecompressBlock<int64_t>();
				break;
			case StenosUInt64:
				ret = new TimeTraceDecompressBlock<uint64_t>();
				break;
			case StenosFloat32:
				ret = new TimeTraceDecompressBlock<float>();
				break;
			case StenosFloat64:
				ret = new TimeTraceDecompressBlock<double>();
				break;
			default:
				break;
		}
	}
	catch (...) {
	}
	return ret;
}

stenos_vblock_header stenos_read_vblock_header_buffer(stenos_payload buffer, uint64_t * full_block_size)
{
	stenos_vblock_header ret;

	if (full_block_size)
		*full_block_size = 0;

	if (buffer.size < sizeof(ret) + 8) {
		memset(&ret, 0, sizeof(ret));
	}
	else {
		memcpy(&ret, (char*)buffer.data + 8, sizeof(ret));
		if (ret.version == 0 || ret.version > STENOS_VIDEO_TRACE_VERSION || ret.pixel_type > StenosFloat64)
			memset(&ret, 0, sizeof(ret));
		else if (full_block_size)
			*full_block_size = read_LE_64(buffer.data)+8;
	}
	return ret;
}

stenos_vblock_header stenos_read_vblock_header_stream(stenos_input* input, uint64_t* full_block_size)
{
	stenos_vblock_header ret;
	auto pos = input->tell(input->opaque);
	char tmp[sizeof(ret) + 8];

	if (full_block_size)
		*full_block_size = 0;

	if (input->read(tmp, (int64_t)sizeof(tmp), input->opaque) != (int64_t)sizeof(tmp)) {
		memset(&ret, 0, sizeof(ret));
	}
	else
		memcpy(&ret, tmp + 8, sizeof(ret));

	if (ret.version == 0 || ret.version > STENOS_VIDEO_TRACE_VERSION || ret.pixel_type > StenosFloat64)
		memset(&ret, 0, sizeof(ret));
	else if (full_block_size)
		*full_block_size = read_LE_64(tmp)+8;

	input->seek(pos, SEEK_SET, input->opaque);
	return ret;
}

stenos_vdecompress* stenos_vdecompress_make_buffer(stenos_payload buffer)
{
	stenos_vdecompress* ret = nullptr;
	stenos_vblock_header h = stenos_read_vblock_header_buffer(buffer, nullptr);
	if (h.version == 0 )
		return ret;

	stenos_pixel_type ptype = (stenos_pixel_type)h.pixel_type;

	try {
		std::unique_ptr<BaseTimeTraceDecompressBlock> dec(from_type(ptype));
		if (dec) {
			ret = new stenos_vdecompress();
			ret->dec = std::move(dec);
			ret->pixel_type = ptype;
			ret->buffer.buf = buffer;
			ret->input.opaque = &ret->buffer;
			ret->input.read = read_stream;
			ret->input.seek = seek_stream;
			ret->input.tell = tell_stream;
			if (!ret->dec->open(&ret->input, true)) {
				delete ret;
				ret = nullptr;
			}
		}
	}
	catch (...) {
		if (ret)
			delete ret;
		ret = nullptr;
	}
	return ret;
}

stenos_vdecompress* stenos_vdecompress_make_stream(stenos_input* read)
{
	stenos_vdecompress* ret = nullptr;
	stenos_vblock_header h = stenos_read_vblock_header_stream(read, nullptr);
	if (h.version == 0)
		return ret;

	stenos_pixel_type ptype = (stenos_pixel_type)h.pixel_type;

	try {
		std::unique_ptr<BaseTimeTraceDecompressBlock> dec(from_type(ptype));
		if (dec) {
			ret = new stenos_vdecompress();
			ret->dec = std::move(dec);
			ret->pixel_type = ptype;
			ret->input = *read;
			if (!ret->dec->open(&ret->input, true)) {
				delete ret;
				ret = nullptr;
			}
		}
	}
	catch (...) {
		if (ret)
			delete ret;
		ret = nullptr;
	}
	return ret;
}

void stenos_vdecompress_set_threads(stenos_vdecompress* ctx, int threads)
{
	if (threads < 1)
		threads = 1;
	ctx->dec->set_threads(threads);
}

void stenos_vdecompress_destroy(stenos_vdecompress* ctx)
{
	if (ctx)
		delete ctx;
}

stenos_vblock_header stenos_vdecompress_info(stenos_vdecompress* ctx)
{
	return (const stenos_vblock_header&)ctx->dec->header();
}

int64_t* stenos_vdecompress_get_timestamps(stenos_vdecompress* ctx)
{
	return (int64_t*)ctx->dec->times().data();
}

size_t stenos_vdecompress_read_image(stenos_vdecompress* ctx, int pos, int inner_stride, void* out_image)
{
	if (pos < 0 || pos >= (int)ctx->dec->header().count)
		return STENOS_ERROR_INVALID_PARAMETER;
	if (ctx->dec->read(out_image, (size_t)inner_stride))
		return 0;
	return STENOS_ERROR_INVALID_INPUT;
}

STENOS_ALWAYS_INLINE static void max_value(double& dst, double v)
{
	if (v > dst)
		dst = v;
}
STENOS_ALWAYS_INLINE static void min_value(double& dst, double v)
{
	if (v < dst)
		dst = v;
}
STENOS_ALWAYS_INLINE static void sum_value(double& dst, double v)
{
	dst += v;
}
STENOS_ALWAYS_INLINE static void sum2_value(double& dst, double v)
{
	dst += v * v;
}

template<class TraceRange>
static bool insert_time_trace(int components, TraceRange range, const int64_t* times, double* max_vals, double* min_vals, double* mean_vals, double* var_vals, size_t count)
{
	if (range.first == range.second)
		return false;

	// insert first
	STENOS_ASSERT_DEBUG(range.first->index < count);
	if (components & StenosTraceMax)
		max_value(max_vals[range.first->index], (double)(range.first->value));
	if (components & StenosTraceMin)
		min_value(min_vals[range.first->index], (double)(range.first->value));
	if (components & StenosTraceMean)
		sum_value(mean_vals[range.first->index], (double)(range.first->value));
	if (components & StenosTraceVar)
		sum2_value(var_vals[range.first->index], (double)(range.first->value));
	++range.first;

	while (range.first != range.second) {
		// use slope between range.first-1 and range.first
		auto point1 = range.first - 1;
		auto point2 = range.first;

		double point1_value = (double)(point1->value);
		double point2_value = (double)(point2->value);

		if (point1->index + 1 == point2->index) {
			STENOS_ASSERT_DEBUG(point2->index < count);
			if (components & StenosTraceMax)
				max_value(max_vals[point2->index], point2_value);
			if (components & StenosTraceMin)
				min_value(min_vals[point2->index], point2_value);
			if (components & StenosTraceMean)
				sum_value(mean_vals[point2->index], point2_value);
			if (components & StenosTraceVar)
				sum2_value(var_vals[point2->index], point2_value);
			++range.first;
			continue;
		}
		double sl = (point2_value - point1_value) / (double)(times[point2->index] - times[point1->index]);
		double beta = point1->value - sl * times[point1->index];
		unsigned start = point1->index + 1;
		unsigned end = point2->index;
		while (start + 3 < end) {
			double v1 = times[start] * sl + beta;
			double v2 = times[(start + 1)] * sl + beta;
			double v3 = times[(start + 2)] * sl + beta;
			double v4 = times[(start + 3)] * sl + beta;
			STENOS_ASSERT_DEBUG(start + 3 < count);
			if (components & StenosTraceMax) {
				max_value(max_vals[start], (v1));
				max_value(max_vals[start + 1], (v2));
				max_value(max_vals[start + 2], (v3));
				max_value(max_vals[start + 3], (v4));
			}
			if (components & StenosTraceMin) {
				min_value(min_vals[start], (v1));
				min_value(min_vals[start + 1], (v2));
				min_value(min_vals[start + 2], (v3));
				min_value(min_vals[start + 3], (v4));
			}
			if (components & StenosTraceMean) {
				sum_value(mean_vals[start], (v1));
				sum_value(mean_vals[start + 1], (v2));
				sum_value(mean_vals[start + 2], (v3));
				sum_value(mean_vals[start + 3], (v4));
			}
			if (components & StenosTraceVar) {
				sum2_value(var_vals[start], (v1));
				sum2_value(var_vals[start + 1], (v2));
				sum2_value(var_vals[start + 2], (v3));
				sum2_value(var_vals[start + 3], (v4));
			}
			start += 4;
		}
		while (start < end) {
			double v = times[start] * sl + beta;
			STENOS_ASSERT_DEBUG(start < count);
			if (components & StenosTraceMax)
				max_value(max_vals[start], (v));
			if (components & StenosTraceMin)
				min_value(min_vals[start], (v));
			if (components & StenosTraceMean)
				sum_value(mean_vals[start], (v));
			if (components & StenosTraceVar)
				sum2_value(var_vals[start], (v));
			++start;
		}
		STENOS_ASSERT_DEBUG(point2->index < count);
		if (components & StenosTraceMax)
			max_value(max_vals[point2->index], point2_value);
		if (components & StenosTraceMin)
			min_value(min_vals[point2->index], point2_value);
		if (components & StenosTraceMean)
			sum_value(mean_vals[point2->index], point2_value);
		if (components & StenosTraceVar)
			sum2_value(var_vals[point2->index], point2_value);
		++range.first;
	}
	return true;
}

struct Error
{
	const char* error = nullptr;
};

template<class T>
static Error compute_block_time_trace(int threads, int components, stenos_input* block, const stenos_coordinate* pixels, size_t pixel_count, stenos_trace_result& ret, size_t& count, stenos_lock* lock)
{
	TimeTraceDecompressBlock<T> bl;
	bl.set_threads(threads < 1 ? 1 : threads);

	// TODO
	// std::unique_lock<std::mutex> lock(read_mutex());

	// Use partial decompression.
	auto traces = bl.open_parallel_partial( block, pixels, pixel_count, lock);
	if (traces.empty())
		return { "empty time trace" };

	count = bl.header().count;

	// retrieve time vector
	const auto& times = bl.times();
	if (times.size() != count)
		return { "invalid time trace size" };

	// Copy times and initialize all values
	for (size_t i = 0; i < times.size(); ++i) {
		ret.timestamps[i] = times[i];
		if (ret.max_values)
			ret.max_values[i] = std::numeric_limits<double>::lowest();
		if (ret.min_values)
			ret.min_values[i] = std::numeric_limits<double>::max();
		if (ret.mean_values)
			ret.mean_values[i] = 0;
		if (ret.var_values)
			ret.var_values[i] = 0;
	}

	// Aggregate each time trace
	for (auto& range : traces)
		if (!insert_time_trace(components,
				       range,
				       ret.timestamps,
				       ret.max_values,
				       ret.min_values,
				       ret.mean_values,
				       ret.var_values,
				       count))
			return { "unable to compute time trace" };

	// Finish computation
	for (size_t i = 0; i < count; ++i) {
		if (components & StenosTraceMean) {
			ret.mean_values[i ] /= (double)traces.size();
		}
		if (components & StenosTraceVar) {
			double& var = ret.var_values[i];			// This is the sum of square values
			var = var / (double)traces.size();				// Mean square
			var -= ret.mean_values[i] * ret.mean_values[i]; // Now we have the variance
			var = std::abs(var);
		}
	}

	return {};
}

template<class T>
static Error compute_trace_block(stenos_input* in, const stenos_trace_query& query, stenos_trace_result& ret, size_t& count, stenos_lock* lock)
{
	return compute_block_time_trace<T>(query.threads, query.components, in, query.pixels, query.pixel_count, ret, count, lock);
}

static Error compute_trace_block_type(stenos_pixel_type type, stenos_input* in, const stenos_trace_query& query, stenos_trace_result& ret, size_t& count, stenos_lock* lock)
{
	switch (type) {
		case StenosUInt8:
			return compute_trace_block<uint8_t>(in, query, ret, count, lock);
		case StenosInt8:
			return compute_trace_block<int8_t>(in, query, ret, count, lock);
		case StenosUInt16:
			return compute_trace_block<uint16_t>(in, query, ret, count, lock);
		case StenosInt16:
			return compute_trace_block<int16_t>(in, query, ret, count, lock);
		case StenosUInt32:
			return compute_trace_block<uint32_t>(in, query, ret, count, lock);
		case StenosInt32:
			return compute_trace_block<int32_t>(in, query, ret, count, lock);
		case StenosUInt64:
			return compute_trace_block<uint64_t>(in, query, ret, count, lock);
		case StenosInt64:
			return compute_trace_block<int64_t>(in, query, ret, count, lock);
		case StenosFloat32:
			return compute_trace_block<float>(in, query, ret, count, lock);
		case StenosFloat64:
			return compute_trace_block<double>(in, query, ret, count, lock);
		default:
			return { "invalid pixel type" };
	}
}

size_t stenos_extract_time_trace(stenos_input* in, stenos_trace_query* query, stenos_trace_result* out_trace, stenos_lock* lock)
{
	if ((query->components & StenosTraceMax) && !out_trace->max_values)
		return STENOS_ERROR_INVALID_PARAMETER;
	if ((query->components & StenosTraceMin) && !out_trace->min_values)
		return STENOS_ERROR_INVALID_PARAMETER;
	if ((query->components & StenosTraceMean) && !out_trace->mean_values)
		return STENOS_ERROR_INVALID_PARAMETER;
	if ((query->components & StenosTraceVar) && !out_trace->var_values)
		return STENOS_ERROR_INVALID_PARAMETER;
	if (!query->pixels || !query->pixel_count)
		return STENOS_ERROR_INVALID_PARAMETER;
	if (query->components < 0 || query->components > StenosTraceAll)
		return STENOS_ERROR_INVALID_PARAMETER;

	auto h = stenos_read_vblock_header_stream(in, nullptr);
	if (h.version == 0)
		return STENOS_ERROR_INVALID_INPUT;

	size_t count = 0;
	auto err = compute_trace_block_type((stenos_pixel_type)h.pixel_type, in, *query, *out_trace, count, lock);
	if(err.error)
		return STENOS_ERROR_INVALID_INPUT;
	return count;
}