#include "../../video_stream.h"
#include "../tiny_pool.h"

#include <mutex>
#include <atomic>
#include <algorithm>

namespace stenos
{
	struct Stream
	{
		std::istream* iss;
		std::mutex* lock;
		int64_t start = 0;

		std::unique_lock<std::mutex> guard()
		{
			if (lock)
				return std::unique_lock<std::mutex>(*lock);
			return std::unique_lock<std::mutex>();
		}
	};
	int64_t read_stream(char* p, int64_t size, void* o)
	{
		Stream* s = (Stream*)o;
		auto guard = s->guard();

		s->iss->read(p, size);
		if (*s->iss)
			return s->iss->gcount();
		return -1;
	}
	int64_t seek_stream(int64_t pos, int w, void* o)
	{
		Stream* s = (Stream*)o;
		auto guard = s->guard();
		if (w == SEEK_SET)
			s->iss->seekg(s->start + pos);
		else 
			s->iss->seekg(pos, std::ios::cur);
		
		if (*s->iss)
			return s->iss->tellg() - s->start;
		return -1;
	}
	int64_t tell_stream(void* o)
	{
		Stream* s = (Stream*)o;
		auto guard = s->guard();
		return s->iss->tellg() - s->start;
	}

	stenos_input make_input(Stream* in)
	{
		stenos_input r;
		r.read = read_stream;
		r.seek = seek_stream;
		r.tell = tell_stream;
		r.opaque = in;
		return r;
	}

	using lock_type = std::mutex;

	struct ImagePos
	{
		int64_t time;
		uint64_t file_block_pos;
		uint64_t block_pos;
	};

	class video_stream::PrivateData
	{
	public:
		lock_type lock;
		std::vector<ImagePos> timestamps; // time -> block position in file
		std::fstream file;
		stenosv_decompress* dec = nullptr;
		int64_t dec_block_pos = 0;
		stream_parameters params;
		std::atomic<uint64_t> error{ 0 };

		void sort_times()
		{
			// Sort timestamps
			std::sort(timestamps.begin(), timestamps.end(), [](const auto& l, const auto& r) { return l.time < r.time; });
		}

		bool hasTime(int64_t time) const noexcept
		{
			auto it = std::lower_bound(timestamps.begin(), timestamps.end(), time, [](const auto& l, const auto& r) { return l.time < r; });
			if (it == timestamps.end() || it->time != time)
				return false;
			return true;
		}
	};

#define RETURN_ERROR(error_code, ...)                                                                                                                                                                  \
	do {                                                                                                                                                                                           \
		d_data->error.store(error_code);                                                                                                                                                       \
		return __VA_ARGS__;                                                                                                                                                                    \
	} while (0)

	video_stream::video_stream()
	{
		d_data = new PrivateData();
	}
	video_stream::video_stream(const char* filename, const stream_parameters& params)
	  : video_stream()
	{
		open(filename, params);
	}
	video_stream::~video_stream() noexcept
	{
		close();
		delete d_data;
	}

	bool video_stream::open(const char* filename, const stream_parameters& params)
	{
		std::lock_guard<lock_type> lock(d_data->lock);

		closeNoLock();

		d_data->params = params;
		if (params.mode < ReadOnly || params.mode > ReadWriteTrunc) {
			closeNoLock();
			RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);
		}

		// open file
		if (params.mode == ReadOnly)
			d_data->file.open(filename, std::ios::in | std::ios::binary);
		else if (params.mode == WriteOnly)
			d_data->file.open(filename, std::ios::out | std::ios::binary);
		else if (params.mode == ReadWrite)
			d_data->file.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
		else
			d_data->file.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);

		if (!d_data->file) {
			closeNoLock();
			RETURN_ERROR(STENOS_ERROR_INVALID_FILENAME, false);
		}

		if (params.mode == ReadOnly || params.mode == ReadWrite) {
			// Get file size
			d_data->file.seekg(0, std::ios::end);
			auto size = d_data->file.tellg();
			d_data->file.seekg(0);
			if (size) {
				// Read blocks
				std::vector<time_type> times(1000);
				Stream str{ &d_data->file, nullptr };
				auto in = make_input(&str);

				uint64_t bs;
				auto h = stenosv_read_block_header_stream(&in, &bs);
				if (h.version == 0) {
					closeNoLock();
					RETURN_ERROR(STENOS_ERROR_INVALID_INPUT, false);
				}

				while (d_data->file.tellg() < size) {
					uint64_t pos = (uint64_t)d_data->file.tellg();
					auto s = stenosv_extract_timestamps(&in, times.data(), times.size());
					if (stenos_has_error(s)) {
						closeNoLock();
						RETURN_ERROR(s, false);
					}

					for (size_t i = 0; i < s; ++i) {
						d_data->timestamps.push_back({ times[i], pos });
					}
				}

				// Check end of file
				if (d_data->file.tellg() != size) {
					closeNoLock();
					RETURN_ERROR(STENOS_ERROR_INVALID_INPUT, false);
				}

				// Sort timestamps
				d_data->sort_times();

				d_data->params.width = h.width;
				d_data->params.height = h.height;
				d_data->params.pixel_type = h.pixel_type;
			}
			else {
				// Empty file : Check parameters
				if (params.mode == ReadOnly) {
					closeNoLock(); // empty file
					RETURN_ERROR(STENOS_ERROR_INVALID_INPUT, false);
				}

				if (d_data->params.width == 0 || d_data->params.height == 0 || d_data->params.pixel_type < 0) {
					closeNoLock();
					RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false); // Invalid input parameters
				}
			}
		}
		else {
			if (d_data->params.width == 0 || d_data->params.height == 0 || d_data->params.pixel_type < 0) {
				closeNoLock();
				RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false); // Invalid input parameters
			}
		}

		if (d_data->params.threads < 1)
			d_data->params.threads = 1;
		if (d_data->params.pixel_type < 0 || d_data->params.pixel_type > StenosFloat64) {
			closeNoLock();
			RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false); // Invalid input parameters
		}

		return true;
	}
	void video_stream::closeNoLock()
	{
		d_data->file.close();
		d_data->timestamps.clear();
		if (d_data->dec) {
			stenosv_decompress_destroy(d_data->dec);
			d_data->dec = nullptr;
		}
		d_data->params = stream_parameters{};
		d_data->error.store(0);
	}
	void video_stream::close() noexcept
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		closeNoLock();
	}

	bool video_stream::is_open() const noexcept
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		return d_data->params.mode != NotOpen;
	}

	auto video_stream::paremeters() const noexcept -> stream_parameters
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		return d_data->params;
	}
	auto video_stream::last_error() const noexcept -> error_type
	{
		return d_data->error.load();
	}
	auto video_stream::count() const noexcept -> size_type
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		return d_data->timestamps.size();
	}
	auto video_stream::time(size_t pos) const noexcept -> time_type
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		if (pos >= d_data->timestamps.size())
			return invalid_time;
		return d_data->timestamps[pos].time;
	}
	bool video_stream::has_time(time_type t) const noexcept
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		return d_data->hasTime(t);
	}
	bool video_stream::has_times(time_type* start, time_type *end) const noexcept
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		auto it = d_data->timestamps.begin();

		for (; start != end; ++start) {
			it = std::lower_bound(it, d_data->timestamps.end(), *start, [](const auto& l, const auto& r) { return l.time < r; });
			if (it == d_data->timestamps.end() || it->time != *start)
				return false;
		}
		return true;
	}
	auto video_stream::timestamps() const noexcept -> std::vector<time_type>
	{
		std::lock_guard<lock_type> lock(d_data->lock);
		std::vector<time_type> ret(d_data->timestamps.size());
		for (size_t i = 0; i < d_data->timestamps.size(); ++i)
			ret[i] = d_data->timestamps[i].time;
		return ret;
	}

	void video_stream::resetError()
	{
		d_data->error.store(0);
	}

	struct Unlocker
	{
		lock_type* lock;
		Unlocker(lock_type* l) noexcept
		  : lock(l)
		{
			l->unlock();
		}
		~Unlocker() noexcept { lock->lock(); }
	};

	struct Compressor
	{
		stenosv_compress* comp = nullptr;

		Compressor() noexcept = default;
		~Compressor() noexcept
		{
			if (comp)
				stenosv_compress_destroy(comp);
		}

		bool init(const stream_parameters& p)
		{ 
			if (comp) {
				int w = stenosv_compress_width(comp);
				int h = stenosv_compress_height(comp);
				int d = stenosv_compress_device(comp);
				int gop = stenosv_compress_gop(comp);
				auto pix = stenosv_compress_pixel_type(comp);
				if (w != p.width || h != p.height || pix != p.pixel_type || d != p.device || gop != p.maxGOP) {
					stenosv_compress_destroy(comp);
					comp = nullptr;
				}
			}
			if (!comp)
				comp = stenosv_compress_make((stenosv_pixel_type)p.pixel_type, p.width, p.height, p.maxGOP, p.device);
			if (!comp)
				return false;
			stenosv_compress_set_threads(comp, (int)p.threads);
			stenosv_compress_set_clevel(comp, (int)p.level);
			stenosv_compress_set_max_error(comp, p.error);
			return true;
		}
	};

	bool video_stream::add_images(const void* imgs, const int64_t* timestamps, size_t image_count)
	{
		struct Payload
		{
			std::vector<char> compressed;
			size_t im_count;
		};

		thread_local Compressor comp;
		

		std::unique_lock<lock_type> lock(d_data->lock);

		resetError();

		if (d_data->params.mode != WriteOnly && d_data->params.mode != ReadWrite && d_data->params.mode != ReadWriteTrunc)
			RETURN_ERROR(STENOS_ERROR_INVALID_IO, false);

		if (!comp.init(d_data->params))
			RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);

		std::vector<Payload> compressed;

		int w = d_data->params.width;
		int h = d_data->params.height;
		int pix = d_data->params.pixel_type;
		size_t image_size = w * h * stenosv_sizeof_pixel_type((stenosv_pixel_type)pix);

		{
			Unlocker unlock(&d_data->lock);
			uint8_t* pimg = (uint8_t*)imgs;
			size_t count = 0;
			for (size_t i = 0; i < image_count; ++i) {

				auto r = stenosv_compress_add_image(comp.comp, pimg, timestamps[i]);
				if (stenos_has_error(r))
					RETURN_ERROR(r, false);

				++count;
				if (r == 1) {
					stenosv_payload p = stenosv_compress_payload(comp.comp);
					compressed.push_back(Payload{ std::vector<char>((char*)p.data, (char*)p.data + p.size), count });
					count = 0;
				}
				pimg += image_size;
			}

			auto r = stenosv_compress_stop(comp.comp);
			if (stenos_has_error(r))
				RETURN_ERROR(r, false);

			stenosv_payload p = stenosv_compress_payload(comp.comp);
			if (p.size)
				compressed.push_back(Payload{ std::vector<char>((char*)p.data, (char*)p.data + p.size), count });
		}

		// Recheck parameters
		if( w != d_data->params.width || h != d_data->params.height ||  pix != d_data->params.pixel_type)
			RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);

		// Check timestamps
		for (size_t i = 0; i < image_count; ++i)
			if (d_data->hasTime(timestamps[i]))
				RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);

		d_data->file.seekp(0, std::ios::end);
		uint64_t file_pos = (uint64_t)d_data->file.tellp();

		for (auto& payload : compressed) {
			d_data->file.write(payload.compressed.data(), payload.compressed.size());
			if (!d_data->file)
				RETURN_ERROR(STENOS_ERROR_INVALID_IO, false);
			for (size_t i = 0; i < payload.im_count; ++i)
				d_data->timestamps.push_back({ timestamps[i], file_pos, i });

			timestamps += payload.im_count;
			file_pos = (uint64_t)d_data->file.tellp();
		}

		d_data->sort_times();

		return true;
	}

	bool video_stream::readImageNoLock(size_t pos, void* img)
	{
		resetError();

		if (d_data->params.mode != ReadOnly && d_data->params.mode != ReadWrite && d_data->params.mode != ReadWriteTrunc)
			RETURN_ERROR(STENOS_ERROR_INVALID_IO, false);

		if (pos >= d_data->timestamps.size())
			RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);

		auto file_pos = d_data->timestamps[pos].file_block_pos;
		if (!d_data->dec || d_data->dec_block_pos != file_pos) {

			if (d_data->dec) {
				stenosv_decompress_destroy(d_data->dec);
				d_data->dec = nullptr;
			}
			// Open block
			d_data->file.seekg(file_pos);
			Stream stream{ &d_data->file, nullptr };
			auto in = make_input(&stream);

			d_data->dec = stenosv_decompress_make_stream(&in, (int)d_data->params.threads);
			if (!d_data->dec)
				RETURN_ERROR(STENOS_ERROR_INVALID_INPUT, false);
			d_data->dec_block_pos = file_pos;
		}

		auto r = stenosv_decompress_read_image(d_data->dec, d_data->timestamps[pos].block_pos, 1, img);
		if (stenos_has_error(r)) {
			d_data->error.store(r);
			return false;
		}
		return true;
	}

	bool video_stream::read_image(size_t pos, void* img)
	{
		std::unique_lock<lock_type> lock(d_data->lock);
		return readImageNoLock(pos, img);
	}

	bool video_stream::read_image_time(time_type time, void* img)
	{
		std::unique_lock<lock_type> lock(d_data->lock);
		auto it = std::lower_bound(d_data->timestamps.begin(), d_data->timestamps.end(), time, [](const auto& l, const auto& r) { return l.time < r; });
		if (it == d_data->timestamps.end() || it->time != time)
			RETURN_ERROR(STENOS_ERROR_INVALID_PARAMETER, false);
		return readImageNoLock((size_t)(it - d_data->timestamps.begin()), img);
	}

	static void lock_mutex(void* opaque)
	{
		static_cast<std::unique_lock<std::mutex>*>(opaque)->lock();
	}
	static void unlock_mutex(void* opaque)
	{
		static_cast<std::unique_lock<std::mutex>*>(opaque)->unlock();
	}

	size_t video_stream::extract_time_trace(const stenosv_trace_query& query, stenosv_trace_result& out, time_type first_time, time_type last_time)
	{
		struct Block
		{
			uint64_t pos = 0;
			uint64_t start_frame = 0;
			uint64_t count = 0;
		};

		try {

			std::unique_lock<std::mutex> lock(d_data->lock);

			if (d_data->timestamps.empty())
				return 0;

			std::pair<int64_t, int64_t> time_bounds = { d_data->timestamps[0].time, d_data->timestamps.back().time };
			if (first_time != invalid_time)
				time_bounds.first = first_time;
			if (last_time != invalid_time)
				time_bounds.second = last_time;

			if (time_bounds.second < time_bounds.first)
				return 0;

			stenosv_trace_query q = query;
			int threads = query.threads;
			if (threads < 1)
				threads = 1;
			q.threads = 1;

			std::atomic<size_t> err{ 0 };
			std::atomic<size_t> ret{ 0 };

			// Build blocks
			std::vector<Block> blocks;
			blocks.push_back(Block{ d_data->timestamps[0].file_block_pos, 0, 1 });
			for (size_t i = 1; i < d_data->timestamps.size(); ++i) {
				if (d_data->timestamps[i].file_block_pos == blocks.back().pos)
					blocks.back().count++;
				else 
					blocks.push_back(Block{ d_data->timestamps[i].file_block_pos, i, 1 });
			}

			// Prevent concurrent file read
			std::mutex mutex;
			

			stenos::get_pool().loop_for(threads, 0, (int)blocks.size(), 1, [&](auto i) {
				if (err.load())
					return;

				size_t idx = (size_t)i;

				auto start_t = d_data->timestamps[blocks[idx].start_frame].time;
				auto last_t = d_data->timestamps[blocks[idx].start_frame + blocks[idx].count - 1].time;

				if (last_t < time_bounds.first || start_t > time_bounds.second)
					return;

				Stream p{ &d_data->file, nullptr, (int64_t)blocks[idx].pos };
				stenos_input stream = make_input(&p);

				stenosv_trace_result r;
				r.timestamps = out.timestamps ? (out.timestamps + blocks[idx].start_frame) : nullptr;
				r.max_values = out.max_values ? (out.max_values + blocks[idx].start_frame) : nullptr;
				r.min_values = out.min_values ? (out.min_values + blocks[idx].start_frame) : nullptr;
				r.mean_values = out.mean_values ? (out.mean_values + blocks[idx].start_frame) : nullptr;
				r.var_values = out.var_values ? (out.var_values + blocks[idx].start_frame) : nullptr;
				r.min_pos = out.min_pos ? (out.min_pos + blocks[idx].start_frame) : nullptr;
				r.max_pos = out.max_pos ? (out.max_pos + blocks[idx].start_frame) : nullptr;

				std::unique_lock<std::mutex> guard(mutex);

				// Seek to the correct location after locking the mutex
				d_data->file.seekg(p.start);

				stenos_lock slock;
				slock.opaque = &guard;
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

}