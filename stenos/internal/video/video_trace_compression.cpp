#include <memory>
#include <mutex>

#include "../../stenos_video.h"
#include "TimeTraceCompress.h"

using namespace stenos;

static std::vector<stenosv_gpu_device> compute_gpu_list()
{
	std::vector<stenosv_gpu_device> devices;

#ifdef STENOS_OPENCL
	if (!getSupportedGPUs().empty()) {
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
	return devices;
}

stenosv_gpu_device* stenosv_list_gpu_devices(int* count)
{
	try {
		static std::vector<stenosv_gpu_device> devices = compute_gpu_list();
		*count = (int)devices.size();
		return devices.empty() ? nullptr : devices.data();
	}
	catch (...) {
		*count = 0;
		return nullptr;
	}
}

int stenosv_default_gpu_device()
{
	try {
#ifdef STENOS_OPENCL
		static int default_device = getDefaultGPU();
#else
		static int default_device = -1;
#endif
		return default_device;
	}
	catch (...) {
		return -1;
	}
}


size_t stenosv_sizeof_pixel_type(stenosv_pixel_type type)
{
	switch(type) {
		case StenosInt8:
		case StenosUInt8:
			return 1;
		case StenosInt16:
		case StenosUInt16:
			return 2;
		case StenosInt32:
		case StenosUInt32:
			return 4;
		case StenosInt64:
		case StenosUInt64:
			return 8;
		case StenosFloat32:
			return 4;
		case StenosFloat64:
			return 8;
		default:
			return 0;
	}
}

struct stenosv_compress_s
{
	std::unique_ptr<BaseTimeTraceCompress> compress;
	stenosv_pixel_type pixel_type;
	std::string payload;
};

stenosv_compress* stenosv_compress_make(stenosv_pixel_type type, int width, int height, int GOP, int device)
{
	stenosv_compress* ret = nullptr;

	try {
		ret = new stenosv_compress();
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

void stenosv_compress_destroy(stenosv_compress* ctx)
{
	if (ctx)
		delete ctx;
}

void stenosv_compress_set_threads(stenosv_compress* ctx, int threads)
{
	if (threads < 1)
		threads = 1;
	ctx->compress->set_threads(threads);
}
void stenosv_compress_set_clevel(stenosv_compress* ctx, int level)
{
	if (level < 0)
		level = 0;
	else if (level > 9)
		level = 9;
	ctx->compress->set_compression_level(level);
}

void stenosv_compress_set_max_time(stenosv_compress* ctx, uint64_t max_nanoseconds)
{
	ctx->compress->set_max_time(max_nanoseconds);
}

void stenosv_compress_set_max_error(stenosv_compress* ctx, double error)
{
	ctx->compress->set_error(error);
}

stenosv_pixel_type stenosv_compress_pixel_type(stenosv_compress* ctx)
{
	return ctx->pixel_type;
}
int stenosv_compress_width(stenosv_compress* ctx)
{
	return ctx->compress->width();
}
int stenosv_compress_height(stenosv_compress* ctx)
{
	return ctx->compress->height();
}

int stenosv_compress_clevel(stenosv_compress* ctx)
{
	return ctx->compress->compression_level();
}
double stenosv_compress_error(stenosv_compress* ctx)
{
	return ctx->compress->error();
}
int stenosv_compress_gop(stenosv_compress* ctx)
{
	return (int)ctx->compress->max_GOP();
}
int stenosv_compress_device(stenosv_compress* ctx)
{
	return ctx->compress->device();
}
int stenosv_compress_threads(stenosv_compress* ctx)
{
	return ctx->compress->threads();
}
uint64_t stenosv_compress_max_time(stenosv_compress* ctx)
{
	return ctx->compress->max_time();
}

size_t stenosv_compress_add_image(stenosv_compress* ctx, void* img, int64_t timestamp)
{
	try {
		const auto& times = ctx->compress->times();
		if (!times.empty() && ctx->compress->current_pos() > 0 && timestamp <= times.back())
			return STENOS_ERROR_INVALID_PARAMETER;

		ctx->payload.clear();
		ctx->payload = ctx->compress->add_frame(img, timestamp);
		if (ctx->payload.empty())
			return 0;
		return 1;
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

size_t stenosv_compress_add_image_bytes(stenosv_compress* ctx, void* img, int inner_stride_bytes, int64_t timestamp)
{
	try {
		const auto& times = ctx->compress->times();
		if (!times.empty() && timestamp <= times.back())
			return STENOS_ERROR_INVALID_PARAMETER;

		ctx->payload.clear();
		ctx->payload = ctx->compress->add_frame_bytes(img, inner_stride_bytes, timestamp);
		if (ctx->payload.empty())
			return 0;
		return 1;
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

size_t stenosv_compress_stop(stenosv_compress* ctx)
{
	try {
		ctx->payload = ctx->compress->finish();
		return 0;
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

stenosv_payload stenosv_compress_payload(stenosv_compress* ctx)
{
	if (ctx->payload.empty())
		return { nullptr, 0 };
	else
		return { ctx->payload.data(), (uint64_t)ctx->payload.size() };
}

struct InputBuffer
{
	stenosv_payload buf{ nullptr, 0 };
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

struct stenosv_decompress_s
{
	std::unique_ptr<BaseTimeTraceDecompressBlock> dec;
	stenos_input input;
	stenosv_pixel_type pixel_type;
	InputBuffer buffer;
};

static BaseTimeTraceDecompressBlock* from_type(stenosv_pixel_type type)
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

stenosv_block_header stenosv_read_block_header_buffer2(void* data, uint64_t size, uint64_t* full_block_size)
{
	stenosv_payload p;
	p.data = data;
	p.size = size;
	return stenosv_read_block_header_buffer(p, full_block_size);
}
stenosv_block_header stenosv_read_block_header_buffer(stenosv_payload buffer, uint64_t* full_block_size)
{
	stenosv_block_header ret;

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
			*full_block_size = read_LE_64(buffer.data) + 8;
	}
	return ret;
}

stenosv_block_header stenosv_read_block_header_stream(stenos_input* input, uint64_t* full_block_size)
{
	try {
		stenosv_block_header ret;
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
			*full_block_size = read_LE_64(tmp) + 8;

		input->seek(pos, SEEK_SET, input->opaque);
		return ret;
	}
	catch (...) {
		stenosv_block_header ret;
		memset(&ret, 0, sizeof(ret));
		return ret;
	}
}

stenosv_decompress* stenosv_decompress_make_buffer(stenosv_payload buffer, int threads)
{
	if (threads < 1)
		threads = 1;

	stenosv_decompress* ret = nullptr;
	stenosv_block_header h = stenosv_read_block_header_buffer(buffer, nullptr);
	if (h.version == 0)
		return ret;

	stenosv_pixel_type ptype = (stenosv_pixel_type)h.pixel_type;

	try {
		std::unique_ptr<BaseTimeTraceDecompressBlock> dec(from_type(ptype));
		if (dec) {
			ret = new stenosv_decompress();
			ret->dec = std::move(dec);
			ret->pixel_type = ptype;
			ret->buffer.buf = buffer;
			ret->input.opaque = &ret->buffer;
			ret->input.read = read_stream;
			ret->input.seek = seek_stream;
			ret->input.tell = tell_stream;
			ret->dec->set_threads(threads);
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

stenosv_decompress* stenosv_decompress_make_stream(stenos_input* read, int threads)
{
	if (threads < 1)
		threads = 1;

	stenosv_decompress* ret = nullptr;
	stenosv_block_header h = stenosv_read_block_header_stream(read, nullptr);
	if (h.version == 0)
		return ret;

	stenosv_pixel_type ptype = (stenosv_pixel_type)h.pixel_type;

	try {
		std::unique_ptr<BaseTimeTraceDecompressBlock> dec(from_type(ptype));
		if (dec) {
			ret = new stenosv_decompress();
			ret->dec = std::move(dec);
			ret->pixel_type = ptype;
			ret->input = *read;
			ret->dec->set_threads(threads);
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

size_t stenosv_extract_timestamps(stenos_input* input, int64_t * timestamps, size_t count)
{
	int threads = 1;

	stenosv_decompress* ret = nullptr;
	stenosv_block_header h = stenosv_read_block_header_stream(input, nullptr);
	if (h.version == 0)
		return STENOS_ERROR_INVALID_INPUT;

	stenosv_pixel_type ptype = (stenosv_pixel_type)h.pixel_type;

	try {
		std::unique_ptr<BaseTimeTraceDecompressBlock> dec(from_type(ptype));
		if (dec) {
			ret = new stenosv_decompress();
			ret->dec = std::move(dec);
			ret->pixel_type = ptype;
			ret->input = *input;
			ret->dec->set_threads(threads);
			bool ok = ret->dec->open(&ret->input, false);
			if(!ok) {
				delete ret;
				return STENOS_ERROR_INVALID_INPUT;
			}

			const auto & times = ret->dec->times();
			if(count < times.size()) {
				delete ret;
				return STENOS_ERROR_INVALID_PARAMETER;
			}
			memcpy(timestamps, times.data(), times.size() * sizeof(int64_t));
			size_t im_count = times.size();
			delete ret;
			return im_count;
		}
	}
	catch (...) {
		if (ret)
			delete ret;
	}
	return STENOS_ERROR_INVALID_INPUT;
}

void stenosv_decompress_set_threads(stenosv_decompress* ctx, int threads)
{
	if (threads < 1)
		threads = 1;
	ctx->dec->set_threads(threads);
}

void stenosv_decompress_destroy(stenosv_decompress* ctx)
{
	if (ctx)
		delete ctx;
}

stenosv_block_header stenosv_decompress_info(stenosv_decompress* ctx)
{
	return (const stenosv_block_header&)ctx->dec->header();
}

int64_t* stenosv_decompress_get_timestamps(stenosv_decompress* ctx)
{
	return (int64_t*)ctx->dec->times().data();
}

size_t stenosv_decompress_read_image(stenosv_decompress* ctx, uint64_t pos, int inner_stride, void* out_image)
{
	try {
		if (pos < 0 || pos >= (int)ctx->dec->header().count)
			return STENOS_ERROR_INVALID_PARAMETER;
		if (!ctx->dec->seek_pos(pos))
			return STENOS_ERROR_INVALID_PARAMETER;
		if (ctx->dec->read(out_image, (size_t)inner_stride))
			return 0;
		return STENOS_ERROR_INVALID_INPUT;
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

size_t stenosv_decompress_read_image_bytes(stenosv_decompress* ctx, uint64_t pos, int inner_stride_bytes, void* out_image)
{
	try {
		if (pos < 0 || pos >= (int)ctx->dec->header().count)
			return STENOS_ERROR_INVALID_PARAMETER;
		if (!ctx->dec->seek_pos(pos))
			return STENOS_ERROR_INVALID_PARAMETER;
		if (ctx->dec->read_bytes(out_image, (size_t)inner_stride_bytes))
			return 0;
		return STENOS_ERROR_INVALID_INPUT;
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

struct Block
{
	int64_t pos = 0;
	uint64_t start_frame = 0;
	uint64_t count = 0;
	std::unique_ptr<stenosv_decompress> dec;
	stenos_input stream;
};

struct PosStream
{
	stenos_input input;
	int64_t pos;
	int64_t start;
};
static int64_t read_stream_pos(char* dst, int64_t count, void* o)
{
	PosStream* s = (PosStream*)o;
	auto r = s->input.read(dst, count, s->input.opaque);
	if (r < 0)
		return r;
	s->pos += r;
	return r;
}
static int64_t seek_stream_pos(int64_t p, int w, void* o)
{
	PosStream* s = (PosStream*)o;
	if (w == SEEK_SET) {
		auto r = s->input.seek(s->start + p, w, s->input.opaque);
		if (r < 0)
			return r;
		s->pos = p;
	}
	else {
		auto r = s->input.seek(p, w, s->input.opaque);
		if (r < 0)
			return r;
		s->pos += p;
	}
	return s->pos;
}
static int64_t tell_stream_pos(void* o)
{
	PosStream* s = (PosStream*)o;
	return s->pos;
}

struct stenosv_bytestream_s
{
	std::vector<int64_t> times;
	std::vector<Block> blocks;
	int width = 0;
	int height = 0;
	int threads = 1;
	stenosv_pixel_type type = StenosInt8;
	stenos_input input;
	uint64_t bytes = 0;
	Block* current = nullptr;
};

stenosv_bytestream* stenosv_bytestream_open(stenos_input input)
{
	std::unique_ptr<stenosv_bytestream> ret;

	try {
		ret.reset(new stenosv_bytestream());

		while (true) {
			uint64_t full_size = 0;
			int64_t pos = input.tell(input.opaque);

			auto h = stenosv_read_block_header_stream(&input, &full_size);

			if (ret->bytes != 0) {
				if (h.version == 0)
					break;
				if (h.pixel_type != ret->type)
					break; // Pixel type mismatch
				if (h.width != ret->width || h.height != ret->height)
					break; // Dimension mismatch
			}
			else if (h.version == 0)
				return nullptr;

			// Read timestamps;
			std::vector<int64_t> times;
			if (input.seek(sizeof(h) + 8, SEEK_CUR, input.opaque) < 0)
				break;
			if (!stenos::compress_detail::read_compressed_vector(1, &input, times, (size_t)h.count * 8))
				break;

			ret->times.insert(ret->times.end(), times.begin(), times.end());
			ret->blocks.push_back(Block{ pos, ret->times.size() - h.count, h.count });

			if (ret->bytes == 0) {
				ret->width = (int)h.width;
				ret->height = (int)h.height;
				ret->type = (stenosv_pixel_type)h.pixel_type;
				ret->input = input;
			}
			ret->bytes += full_size;

			input.seek(pos + (int64_t)full_size, SEEK_SET, input.opaque);
		}
	}
	catch (...) {
		return nullptr;
	}
	if (ret->times.size() == 0)
		return nullptr;
	return ret.release();
}

void stenosv_bytestream_destroy(stenosv_bytestream* ctx)
{
	if (ctx)
		delete ctx;
}

int stenosv_bytestream_width(stenosv_bytestream* ctx)
{
	return ctx->width;
}
int stenosv_bytestream_height(stenosv_bytestream* ctx)
{
	return ctx->height;
}
int stenosv_bytestream_threads(stenosv_bytestream* ctx)
{
	return ctx->threads;
}
stenosv_pixel_type stenosv_bytestream_pixel_type(stenosv_bytestream* ctx)
{
	return ctx->type;
}
size_t stenosv_bytestream_count(stenosv_bytestream* ctx)
{
	return ctx->times.size();
}
uint64_t stenosv_bytestream_bytes(stenosv_bytestream* ctx)
{
	return ctx->bytes;
}
int64_t* stenosv_bytestream_times(stenosv_bytestream* ctx)
{
	return ctx->times.data();
}

void stenosv_bytestream_set_threads(stenosv_bytestream* ctx, int threads)
{
	if (threads < 1)
		threads = 1;
	ctx->threads = threads;
}

size_t stenosv_bytestream_read(stenosv_bytestream* ctx, uint64_t pos, void* img)
{
	if (ctx->current) {
		if (!(pos >= ctx->current->start_frame && pos < (ctx->current->start_frame + ctx->current->count))) {
			ctx->current->dec.reset();
			ctx->current = nullptr;
		}
	}

	if (!ctx->current) {
		// Find block
		auto it = std::lower_bound(ctx->blocks.begin(), ctx->blocks.end(), pos, [](const Block& l, uint64_t p) { return l.start_frame < p; });
		if (it == ctx->blocks.end())
			return STENOS_ERROR_INVALID_INPUT;
		if (it->start_frame > pos)
			--it;

		ctx->current = &(*it);
	}

	if (!ctx->current->dec) {
		// Create decompressor
		PosStream s;
		s.pos = 0;
		s.start = ctx->current->pos;
		s.input = ctx->input;
		if(ctx->input.seek(s.start, SEEK_SET, ctx->input.opaque) < 0)
			return STENOS_ERROR_INVALID_IO;
		stenos_input in;
		in.opaque = &s;
		in.read = read_stream_pos;
		in.seek = seek_stream_pos;
		in.tell = tell_stream_pos;
		ctx->current->dec.reset(stenosv_decompress_make_stream(&in, ctx->threads));
		if (!ctx->current->dec)
			return STENOS_ERROR_INVALID_INPUT;
	}

	return stenosv_decompress_read_image(ctx->current->dec.get(), pos - ctx->current->start_frame, 1, img);
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
static bool insert_time_trace(int components,
			      TraceRange range,
			      const int64_t* times,
			      double* max_vals,
			      double* min_vals,
			      double* mean_vals,
			      double* var_vals,
			      stenosv_coordinate* min_pos,
			      stenosv_coordinate* max_pos,
			      stenosv_coordinate c,
			      size_t count)
{
	if (range.first == range.second)
		return false;

	// insert first
	STENOS_ASSERT_DEBUG(range.first->index < count);

	if ((components & StenosTraceMinPos) && (double)(range.first->value) < min_vals[range.first->index])
		min_pos[range.first->index] = c;
	if ((components & StenosTraceMaxPos) && (double)(range.first->value) > max_vals[range.first->index])
		max_pos[range.first->index] = c;
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

			if ((components & StenosTraceMinPos) && point2_value < min_vals[point2->index])
				min_pos[point2->index] = c;
			if ((components & StenosTraceMaxPos) && point2_value > max_vals[point2->index])
				max_pos[point2->index] = c;
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

			if (components & StenosTraceMinPos) {
				if (v1 < min_vals[start])
					min_pos[start] = c;
				if (v2 < min_vals[start + 1])
					min_pos[start + 1] = c;
				if (v3 < min_vals[start + 2])
					min_pos[start + 2] = c;
				if (v4 < min_vals[start + 3])
					min_pos[start + 3] = c;
			}

			if (components & StenosTraceMaxPos) {
				if (v1 > max_vals[start])
					max_pos[start] = c;
				if (v2 > max_vals[start + 1])
					max_pos[start + 1] = c;
				if (v3 > max_vals[start + 2])
					max_pos[start + 2] = c;
				if (v4 > max_vals[start + 3])
					max_pos[start + 3] = c;
			}

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
			if (components & StenosTraceMinPos) {
				if (v < min_vals[start])
					min_pos[start] = c;
			}
			if (components & StenosTraceMaxPos) {
				if (v > max_vals[start])
					max_pos[start] = c;
			}
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
		if (components & StenosTraceMinPos) {
			if (point2_value < min_vals[point2->index])
				min_pos[start] = c;
		}
		if (components & StenosTraceMaxPos) {
			if (point2_value > max_vals[point2->index])
				max_pos[start] = c;
		}
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
static Error
compute_block_time_trace(int threads, int components, stenos_input* block, const stenosv_coordinate* pixels, size_t pixel_count, stenosv_trace_result& ret, size_t& count, stenos_lock* lock)
{
	TimeTraceDecompressBlock<T> bl;
	bl.set_threads(threads < 1 ? 1 : threads);

	// TODO
	// std::unique_lock<std::mutex> lock(read_mutex());

	// Use partial decompression.
	auto traces = bl.open_parallel_partial(block, pixels, pixel_count, lock);
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
	for (size_t i = 0; i < traces.size(); ++i) {
		auto& range = traces[i];
		if (!insert_time_trace(components, range, ret.timestamps, ret.max_values, ret.min_values, ret.mean_values, ret.var_values, ret.min_pos, ret.max_pos, pixels[i], count))
			return { "unable to compute time trace" };
	}

	// Finish computation
	for (size_t i = 0; i < count; ++i) {
		if (components & StenosTraceMean) {
			ret.mean_values[i] /= (double)traces.size();
		}
		if (components & StenosTraceVar) {
			double& var = ret.var_values[i];		// This is the sum of square values
			var = var / (double)traces.size();		// Mean square
			var -= ret.mean_values[i] * ret.mean_values[i]; // Now we have the variance
			var = std::abs(var);
		}
	}

	return {};
}

template<class T>
static Error compute_trace_block(stenos_input* in, const stenosv_trace_query& query, stenosv_trace_result& ret, size_t& count, stenos_lock* lock)
{
	return compute_block_time_trace<T>(query.threads, query.components, in, query.pixels, query.pixel_count, ret, count, lock);
}

static Error compute_trace_block_type(stenosv_pixel_type type, stenos_input* in, const stenosv_trace_query& query, stenosv_trace_result& ret, size_t& count, stenos_lock* lock)
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

size_t stenosv_extract_time_trace(stenos_input* in, stenosv_trace_query* query, stenosv_trace_result* out_trace, stenos_lock* lock)
{
	try {

		if ((query->components & StenosTraceMax) && !out_trace->max_values)
			return STENOS_ERROR_INVALID_PARAMETER;
		if ((query->components & StenosTraceMin) && !out_trace->min_values)
			return STENOS_ERROR_INVALID_PARAMETER;
		if ((query->components & StenosTraceMean) && !out_trace->mean_values)
			return STENOS_ERROR_INVALID_PARAMETER;
		if ((query->components & StenosTraceVar) && !out_trace->var_values)
			return STENOS_ERROR_INVALID_PARAMETER;
		if ((query->components & StenosTraceMinPos) && !out_trace->min_pos)
			return STENOS_ERROR_INVALID_PARAMETER;
		if ((query->components & StenosTraceMaxPos) && !out_trace->max_pos)
			return STENOS_ERROR_INVALID_PARAMETER;
		if (!query->pixels || !query->pixel_count)
			return STENOS_ERROR_INVALID_PARAMETER;
		if (query->components < 0 || query->components > StenosTraceAll)
			return STENOS_ERROR_INVALID_PARAMETER;

		auto h = stenosv_read_block_header_stream(in, nullptr);
		if (h.version == 0)
			return STENOS_ERROR_INVALID_INPUT;

		size_t count = 0;
		auto err = compute_trace_block_type((stenosv_pixel_type)h.pixel_type, in, *query, *out_trace, count, lock);
		if (err.error)
			return STENOS_ERROR_INVALID_INPUT;
		return count;
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}

static void lock_mutex(void* opaque)
{
	static_cast<std::unique_lock<std::mutex>*>(opaque)->lock();
}
static void unlock_mutex(void* opaque)
{
	static_cast<std::unique_lock<std::mutex>*>(opaque)->unlock();
}

size_t stenosv_bytestream_extract_time_trace(stenosv_bytestream* input, int64_t* start_time, int64_t* end_time, stenosv_trace_query* query, stenosv_trace_result* out_trace)
{
	try {

		auto times = stenosv_bytestream_times(input);
		auto count = stenosv_bytestream_count(input);

		std::pair<int64_t, int64_t> time_bounds = { times[0], times[count - 1] };
		if (start_time)
			time_bounds.first = *start_time;
		if (end_time)
			time_bounds.second = *end_time;

		if (time_bounds.second < time_bounds.first)
			return STENOS_ERROR_INVALID_INPUT;

		stenosv_trace_query q = *query;
		int threads = query->threads;
		if (threads < 1)
			threads = 1;
		q.threads = 1;

		std::atomic<size_t> err{ 0 };
		std::atomic<size_t> ret{ 0 };

		std::mutex mutex;
		

		stenos::get_pool().loop_for(threads, 0, (int)input->blocks.size(), 1, [&](auto i) {
			if (err.load())
				return;

			size_t idx = (size_t)i;

			auto start_t = times[input->blocks[idx].start_frame];
			auto last_t = times[input->blocks[idx].start_frame + input->blocks[idx].count -1];

			if (last_t < time_bounds.first || start_t > time_bounds.second)
				return;

			PosStream p;
			p.pos = 0;
			p.start = input->blocks[idx].pos;
			p.input = input->input;
			
			stenos_input stream;
			stream.opaque = &p;
			stream.read = read_stream_pos;
			stream.seek = seek_stream_pos;
			stream.tell = tell_stream_pos;

			stenosv_trace_result r;
			r.timestamps = out_trace->timestamps ? (out_trace->timestamps + input->blocks[idx].start_frame) : nullptr;
			r.max_values = out_trace->max_values ? (out_trace->max_values + input->blocks[idx].start_frame) : nullptr;
			r.min_values = out_trace->min_values ? (out_trace->min_values + input->blocks[idx].start_frame) : nullptr;
			r.mean_values = out_trace->mean_values ? (out_trace->mean_values + input->blocks[idx].start_frame) : nullptr;
			r.var_values = out_trace->var_values ? (out_trace->var_values + input->blocks[idx].start_frame) : nullptr;
			r.min_pos = out_trace->min_pos ? (out_trace->min_pos + input->blocks[idx].start_frame) : nullptr;
			r.max_pos = out_trace->max_pos ? (out_trace->max_pos + input->blocks[idx].start_frame) : nullptr;

			// Seek to the correct location after locking the mutex
			std::unique_lock<std::mutex> lock(mutex);
			input->input.seek(p.start, SEEK_SET, input->input.opaque);

			stenos_lock slock;
			slock.opaque = &lock;
			slock.lock = lock_mutex;
			slock.unlock = unlock_mutex;

			auto er = stenosv_extract_time_trace(&stream, &q, &r, &slock);
			if (stenos_has_error(er))
				err.store(er);
			else
				ret += er;
		});

		if (auto error = err.load())
			return error;

		return ret.load();
	}
	catch (...) {
		return STENOS_ERROR_ALLOC;
	}
}