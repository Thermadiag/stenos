#include <stenos/stenos_video.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

#include "testing.hpp"


#define STENOS_TEST(...)                                                                                                                                                                               \
	if (!(__VA_ARGS__))                                                                                                                                                                            \
	STENOS_ABORT("Test error in %s line %i\n", __FILE__, __LINE__)


static int64_t read_stream(char* dst, int64_t size, void* opaque)
{
	std::istream* iss = static_cast<std::istream*>(opaque);
	iss->read(dst, size);
	return iss->gcount();
}
static int64_t seek_stream(int64_t pos, int whence, void* opaque)
{
	std::istream* iss = static_cast<std::istream*>(opaque);
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
	return static_cast<std::istream*>(opaque)->tellg();
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

	auto pixel_type = stenos_to_pixel_type<T>();
	std::vector<T> image((size_t)(width * height));

	// Open output video file
	std::ostringstream fout(std::ios::binary);
	STENOS_TEST(fout);

	// Build codec context
	auto codec = stenos_vcompress_make(pixel_type, width, height, GOP, device);
	STENOS_TEST(codec);
	stenos_vcompress_set_clevel(codec,1);
	stenos_vcompress_set_threads(codec,threads);
	stenos_vcompress_set_max_error(codec,error);

	// Write frames images
	for (size_t i = 0; i < frames; ++i) {
		std::fill_n(image.begin(), image.size(), (T)pattern(i));
		auto r = stenos_vcompress_add_image(codec, image.data(), (int64_t)i);
		STENOS_TEST(!stenos_has_error(r));
		if (r == 1) {
			// write block
			auto buffer = stenos_vcompress_payload(codec);
			fout.write((char*)buffer.data, buffer.size);
			STENOS_TEST(fout);
		}
	}
	stenos_vcompress_stop(codec);
	auto buffer = stenos_vcompress_payload(codec);
	fout.write((char*)buffer.data, buffer.size);
	stenos_vcompress_destroy(codec);
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

	while (true) {
		auto codec = stenos_vdecompress_make_stream(&in);
		if (!codec)
			break;
		stenos_vdecompress_set_threads(codec, threads);
		stenos_vblock_header h = stenos_vdecompress_info(codec);
		STENOS_TEST(h.version);
		STENOS_TEST((int)h.width == width);
		STENOS_TEST((int)h.height == height);
		STENOS_TEST((int)h.count <= GOP);
		STENOS_TEST((int)h.pixel_type == pixel_type);

		for (size_t i = 0; i < h.count; ++i, ++pos) {
			
			auto r = stenos_vdecompress_read_image(codec, (int)i, 1, image.data());
			STENOS_TEST(!stenos_has_error(r));

			T expected = (T)pattern(pos);
			for (auto p : image) {
				STENOS_TEST(compare(p, expected,error));
			}
		}
		stenos_vdecompress_destroy(codec);
	}
	STENOS_TEST(pos == frames);

	std::istringstream fin2(fout.str(), std::ios::binary);
	STENOS_TEST(fin2);
	in.opaque = &fin2;
	pos = 0;

	// Test time trace extraction
	stenos_trace_query q;
	stenos_init_trace_query(&q);
	q.components = StenosTraceAll;
	q.threads = threads;
	stenos_coordinate c{ 0, 0 };
	q.pixels = &c;
	q.pixel_count = 1;

	std::vector<int64_t> timestamps(frames);
	std::vector<double> max(frames), min(frames), mean(frames), var(frames);

	while (true) {
		
		uint64_t block_size = 0;
		auto streampos = fin2.tellg();
		auto h = stenos_read_vblock_header_stream(&in, &block_size);
		if (h.version == 0)
			break;
		STENOS_TEST(h.version);
		STENOS_TEST((int)h.width == width);
		STENOS_TEST((int)h.height == height);
		STENOS_TEST((int)h.count <= GOP);
		STENOS_TEST((int)h.pixel_type == pixel_type);

		stenos_trace_result tr;
		tr.timestamps = timestamps.data() + pos;
		tr.min_values = min.data() + pos;
		tr.max_values = max.data() + pos;
		tr.mean_values = mean.data() + pos;
		tr.var_values = var.data() + pos;

		auto r = stenos_extract_time_trace(&in, &q, &tr, nullptr);
		STENOS_TEST(!stenos_has_error(r));
		
		fin2.seekg(streampos + (int64_t)block_size);
		pos += h.count;
	}
	STENOS_TEST(pos == frames);

	for (size_t i = 0; i < frames; ++i) {
		STENOS_TEST(timestamps[i] == (int64_t)i);
		double expected = (double)pattern(i);
		STENOS_TEST(compare(min[i], expected, error));
		STENOS_TEST(compare(max[i], expected, error));
		STENOS_TEST(compare(mean[i], expected, error));
		STENOS_TEST(compare(var[i], 0., error));
	}
	
}

int test_video_codec(int, char*[]) 
{
	int gpu_count = 0;
	auto devices = stenos_list_gpu_devices(&gpu_count);
	for (int i = 0; i < gpu_count; ++i) {
		auto& d = devices[i];
		std::cout << d.name << " " << d.vendor_id << " " << d.compute_units << " " << d.max_memory << std::endl;
	}

	test_codec<int>(100, 100, 30, 0, 12, -1, [](size_t i) { return i; });

	return 0;
}