#include <stenos/stenos_video.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>
#include <iostream>

#include "testing.hpp"
#include "BS_thread_pool.hpp"

#define STENOS_TEST(...)                                                                                                                                                                               \
	if (!(__VA_ARGS__))                                                                                                                                                                            \
	STENOS_ABORT("Test error in %s line %i\n", __FILE__, __LINE__)


static BS::light_thread_pool pool(12);


static int64_t read_stream(char* dst, int64_t size, void* opaque)
{
	std::istream* iss = static_cast<std::istream*>(opaque);
	iss->clear();
	iss->read(dst, size);
	return iss->gcount();
}
static int64_t seek_stream(int64_t pos, int whence, void* opaque)
{
	std::istream* iss = static_cast<std::istream*>(opaque);
	iss->clear();
	if (whence == STENOS_SEEK_SET) {
		if (pos < 0)
			return STENOS_ERROR_INVALID_IO;
		iss->seekg(pos);
	}
	else {
		iss->seekg(pos, std::ios::cur);
	}
	if (!iss)
		return STENOS_ERROR_INVALID_IO;
	return 0;
}
static int64_t tell_stream(void* opaque)
{
	std::istream* iss = static_cast<std::istream*>(opaque);
	return iss->tellg();
}

static void lock_mutex(void* m)
{
	static_cast<std::unique_lock<std::mutex>*>(m)->lock();
}
static void unlock_mutex(void* m)
{
	static_cast<std::unique_lock<std::mutex>*>(m)->unlock();
}

template<class T>
static bool compare(T v1, T v2, double error)
{
	double err = std::abs((double)v1 - (double)v2);
	return err <= error + 0.0000001;
}


template<class T, class Fun>
void test_codec(int width, int height, int GOP, double error, int threads, int device, Fun pattern)
{
	static constexpr size_t frames = 100;

	auto pixel_type = stenosv_to_pixel_type<T>();
	std::vector<T> image((size_t)(width * height));

	// Open output video file
	std::ostringstream fout(std::ios::binary);
	STENOS_TEST(fout);

	// Build codec context
	auto codec = stenosv_compress_make(pixel_type, width, height,error, GOP, device);
	STENOS_TEST(codec);
	stenosv_compress_set_clevel(codec,9);
	stenosv_compress_set_threads(codec,threads);

	// Write frames images
	for (size_t i = 0; i < frames; ++i) {
		std::fill_n(image.begin(), image.size(), (T)pattern(i));
		auto r = stenosv_compress_add_image(codec, image.data(), (int64_t)i);
		STENOS_TEST(!stenos_has_error(r));
		if (r == 1) {
			// write block
			auto buffer = stenosv_compress_payload(codec);
			fout.write((char*)buffer.data, buffer.size);
			STENOS_TEST(fout);
		}
	}
	stenosv_compress_stop(codec);
	auto buffer = stenosv_compress_payload(codec);
	fout.write((char*)buffer.data, buffer.size);
	stenosv_compress_destroy(codec);
	STENOS_TEST(fout);

	// Now, open the file and check its content

	std::istringstream fin(fout.str(), std::ios::binary);
	STENOS_TEST(fin);
	stenos_input in;
	in.opaque = &fin;
	in.read = read_stream;
	in.seek = seek_stream;
	in.tell = tell_stream;

	size_t pos = 0;
	// While extracting time trace, build the map of block position -> block cumulated image count
	std::vector<std::pair<int64_t, int64_t>> blocks;

	while (true) {
		auto streampos = fin.tellg();
		auto codec = stenosv_decompress_make_stream(&in, threads);
		if (!codec)
			break;
		stenosv_block_header h = stenosv_decompress_info(codec);
		STENOS_TEST(h.version);
		STENOS_TEST((int)h.width == width);
		STENOS_TEST((int)h.height == height);
		STENOS_TEST((int)h.count <= GOP);
		STENOS_TEST((int)h.pixel_type == pixel_type);

		blocks.push_back({ (int64_t)streampos, (int64_t)pos });

		for (size_t i = 0; i < h.count; ++i, ++pos) {
			
			auto r = stenosv_decompress_read_image(codec, (int)i, 1, image.data());
			STENOS_TEST(!stenos_has_error(r));

			T expected = (T)pattern(pos);
			for (auto p : image) {
				STENOS_TEST(compare(p, expected,error));
			}
		}
		stenosv_decompress_destroy(codec);
	}
	STENOS_TEST(pos == frames);



	{
		// Test bytestream
		std::istringstream fin2(fout.str(), std::ios::binary);
		STENOS_TEST(fin2);
		in.opaque = &fin2;

		auto bytestream = stenosv_bytestream_open(in);
		STENOS_TEST(bytestream);
		STENOS_TEST(stenosv_bytestream_bytes( bytestream) == fout.str().size());
		STENOS_TEST(stenosv_bytestream_height( bytestream) == height);
		STENOS_TEST(stenosv_bytestream_width(bytestream) == width);
		STENOS_TEST(stenosv_bytestream_pixel_type(bytestream) == pixel_type);
		STENOS_TEST(stenosv_bytestream_count(bytestream) == frames);

		for (size_t i = 0; i < frames; ++i) {
			auto r = stenosv_bytestream_read(bytestream, i, image.data());
			STENOS_TEST(!stenos_has_error(r));
			T expected = (T)pattern(i);
			for (auto p : image) {
				STENOS_TEST(compare(p, expected, error));
			}
		}
		for (int64_t idx = (int64_t)frames - 1; idx >= 0; --idx) {
			size_t i = (size_t)idx;
			auto r = stenosv_bytestream_read(bytestream, i, image.data());
			STENOS_TEST(!stenos_has_error(r));
			T expected = (T)pattern(i);
			for (auto p : image) {
				STENOS_TEST(compare(p, expected, error));
			}
		}

		{
			// Extract time trace on bytestream
			stenosv_trace_query q;
			stenosv_init_trace_query(&q);
			q.components = StenosTraceAll;
			q.threads = threads;
			stenosv_coordinate c{ 0, 0 };
			q.pixels = &c;
			q.pixel_count = 1;

			std::vector<int64_t> timestamps(frames);
			std::vector<double> max(frames), min(frames), mean(frames), var(frames);
			std::vector<stenosv_coordinate> min_pos(frames), max_pos(frames);

			stenosv_trace_result tr;
			tr.timestamps = timestamps.data();
			tr.min_values = min.data() ;
			tr.max_values = max.data() ;
			tr.mean_values = mean.data() ;
			tr.var_values = var.data() ;
			tr.min_pos = min_pos.data() ;
			tr.max_pos = max_pos.data() ;

			auto r = stenosv_bytestream_extract_time_trace(bytestream, NULL, NULL, &q, &tr);
			STENOS_TEST(!stenos_has_error(r));

			for (size_t i = 0; i < frames; ++i) {
				STENOS_TEST(timestamps[i] == (int64_t)i);
				double expected = (double)(T)pattern(i);
				STENOS_TEST(compare(min[i], expected, error));
				STENOS_TEST(compare(max[i], expected, error));
				STENOS_TEST(compare(mean[i], expected, error));
				STENOS_TEST(compare(var[i], 0., error));
			}
		}

		stenosv_bytestream_destroy(bytestream);
	}

	std::istringstream fin2(fout.str(), std::ios::binary);
	STENOS_TEST(fin2);
	in.opaque = &fin2;
	pos = 0;



	// Test time trace extraction
	{
		stenosv_trace_query q;
		stenosv_init_trace_query(&q);
		q.components = StenosTraceAll;
		q.threads = threads;
		stenosv_coordinate c{ 0, 0 };
		q.pixels = &c;
		q.pixel_count = 1;

		std::vector<int64_t> timestamps(frames);
		std::vector<double> max(frames), min(frames), mean(frames), var(frames);
		std::vector<stenosv_coordinate> min_pos(frames), max_pos(frames);

		while (true) {

			uint64_t block_size = 0;
			auto streampos = fin2.tellg();
			auto h = stenosv_read_block_header_stream(&in, &block_size);
			if (h.version == 0)
				break;
			STENOS_TEST(h.version);
			STENOS_TEST((int)h.width == width);
			STENOS_TEST((int)h.height == height);
			STENOS_TEST((int)h.count <= GOP);
			STENOS_TEST((int)h.pixel_type == pixel_type);

			stenosv_trace_result tr;
			tr.timestamps = timestamps.data() + pos;
			tr.min_values = min.data() + pos;
			tr.max_values = max.data() + pos;
			tr.mean_values = mean.data() + pos;
			tr.var_values = var.data() + pos;
			tr.min_pos = min_pos.data() + pos;
			tr.max_pos = max_pos.data() + pos;

			auto r = stenosv_extract_time_trace(&in, &q, &tr, nullptr);
			STENOS_TEST(!stenos_has_error(r));

			fin2.seekg(streampos + (int64_t)block_size);
			pos += h.count;
		}
		STENOS_TEST(pos == frames);

		for (size_t i = 0; i < frames; ++i) {
			STENOS_TEST(timestamps[i] == (int64_t)i);
			double expected = (double)(T)pattern(i);
			STENOS_TEST(compare(min[i], expected, error));
			STENOS_TEST(compare(max[i], expected, error));
			STENOS_TEST(compare(mean[i], expected, error));
			STENOS_TEST(compare(var[i], 0., error));
		}
	}
	

	{
		// Last check: parallel time trace extraction
		stenosv_trace_query q;
		stenosv_init_trace_query(&q);
		q.components = StenosTraceAll;
		q.threads = threads;
		stenosv_coordinate c{ 0, 0 };
		q.pixels = &c;
		q.pixel_count = 1;

		std::vector<int64_t> timestamps(frames);
		std::vector<double> max(frames), min(frames), mean(frames), var(frames);
		std::vector<stenosv_coordinate> min_pos(frames), max_pos(frames);

		std::istringstream fin3(fout.str(), std::ios::binary);
		STENOS_TEST(fin3);
		in.opaque = &fin3;
		std::mutex mutex;

		for (size_t i = 0; i < blocks.size(); ++i) {
			pool.detach_task([&, i]() {

				std::unique_lock<std::mutex> lock(mutex);
				stenos_lock slock{ lock_mutex, unlock_mutex, &lock };

				fin3.seekg(blocks[i].first);
				auto h = stenosv_read_block_header_stream(&in, nullptr);
				
				STENOS_TEST(h.version);
				STENOS_TEST((int)h.width == width);
				STENOS_TEST((int)h.height == height);
				STENOS_TEST((int)h.count <= GOP);
				STENOS_TEST((int)h.pixel_type == pixel_type);

				stenosv_trace_result tr;
				tr.timestamps = timestamps.data() + blocks[i].second;
				tr.min_values = min.data() + blocks[i].second;
				tr.max_values = max.data() + blocks[i].second;
				tr.mean_values = mean.data() + blocks[i].second;
				tr.var_values = var.data() + blocks[i].second;
				tr.min_pos = min_pos.data() + blocks[i].second;
				tr.max_pos = max_pos.data() + blocks[i].second;

				auto r = stenosv_extract_time_trace(&in, &q, &tr, &slock);
				STENOS_TEST(!stenos_has_error(r));
			});
		}
		pool.wait();
		for (size_t i = 0; i < frames; ++i) {
			STENOS_TEST(timestamps[i] == (int64_t)i);
			double expected = (double)(T)pattern(i);
			STENOS_TEST(compare(min[i], expected, error));
			STENOS_TEST(compare(max[i], expected, error));
			STENOS_TEST(compare(mean[i], expected, error));
			STENOS_TEST(compare(var[i], 0., error));
		}
	}
}

int test_video_codec(int, char*[]) 
{
	int gpu_count = 0;
	auto devices = stenosv_list_gpu_devices(&gpu_count);
	for (int i = 0; i < gpu_count; ++i) {
		auto& d = devices[i];
		std::cout << d.name << " " << d.vendor_id << " " << d.compute_units << " " << d.max_memory << std::endl;
	}

	
	for (int device = -1; device < 1; ++device) {

		test_codec<int>(100, 100, 30, 0, 12, device, [](size_t i) { return i; });
		test_codec<int>(100, 100, 30, 1, 12, device, [](size_t i) { return i; });
		test_codec<int>(100, 100, 30, 2, 12, device, [](size_t i) { return i; });
		test_codec<double>(100, 100, 30, 0, 12, device, [](size_t i) { return i; });
		test_codec<double>(100, 100, 30, 1, 12, device, [](size_t i) { return i; });
		test_codec<double>(100, 100, 30, 2, 12, device, [](size_t i) { return i; });

		auto fun2 = [](size_t i) { return std::cos(i * 0.1)*100; };
		test_codec<int>(100, 100, 30, 0, 12, device, fun2);
		test_codec<int>(100, 100, 30, 1, 12, device, fun2);
		test_codec<int>(100, 100, 30, 2, 12, device, fun2);
		test_codec<double>(100, 100, 30, 0, 12, device, fun2);
		test_codec<double>(100, 100, 30, 1, 12, device, fun2);
		test_codec<double>(100, 100, 30, 2, 12, device, fun2);
	}

	return 0;
}