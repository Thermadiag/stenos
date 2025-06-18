#include <fstream>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <sstream>

#include "stenos/stenos.h"
// #ifdef HAS_BLOSC
#include "blosc2.h"
// #endif
#include <zstd.h>

static inline auto msecs_since_epoch() -> uint64_t
{
	using namespace std::chrono;
	return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

template<int rshift>
int get_value(int i)
{
	int v= (i << 26) ^ (i << 18) ^ (i << 11) ^ (i << 3) ^ i;
	if (rshift < 32) {
		v &= (1 << rshift) - 1;
	}
	return v;
}
template<int rshift>
static std::vector<int> synthetic(int size)
{
	std::vector<int> res((size_t)size);
	for (int i = 0; i < size; ++i)
		res[(size_t)i] = get_value<rshift>(i);
	return res;
}

template<size_t N>
static std::vector<std::array<char, N>> read_binary(const char* filename)
{
	std::vector<std::array<char, N>> res;
	std::ifstream iss(filename, std::ios::binary);

	while (iss) {
		std::array<char, N> ar;
		iss.read((char*)&ar, sizeof(ar));
		if (iss)
			res.push_back(ar);
	}
	return res;
}

template<class T>
static std::vector<T> read_text(const char* filename)
{
	std::vector<T> res;
	std::ifstream iss(filename, std::ios::binary);

	while (iss) {
		T val;
		iss >> val;
		if (iss)
			res.push_back(val);
	}
	return res;
}

template<class T>
static int iteration_count(const std::vector<T>& vec)
{
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	if (bytes < 500000)
		return 100;
	if (bytes < 2000000)
		return 50;
	if (bytes < 5000000)
		return 10;
	return 1;
}

template<class T>
static size_t compress_vec_stenos(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	size_t res = 0;
	stenos_context* ctx = stenos_make_context();
	stenos_set_level(ctx, level);
	stenos_set_threads(ctx, (int)threads);
	for (int i = 0; i < iterations; ++i)
		res = stenos_compress_generic(ctx,vec.data(), sizeof(typename std::vector<T>::value_type), bytes, dst, dst_size);
	stenos_destroy_context(ctx);
	if (stenos_has_error(res))
		return 0;
	return res;
}

template<class T>
static size_t compress_vec_blosc2_shuffle(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_bitshuffle(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i) 
		res = blosc2_compress(level, BLOSC_BITSHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_delta_shuffle(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(1);
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_shuffle_zstd(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	blosc1_set_compressor("zstd");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_bitshuffle_zstd(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	blosc1_set_compressor("zstd");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_BITSHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_shuffle_lz4(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	blosc1_set_compressor("lz4");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_SHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}
template<class T>
static size_t compress_vec_blosc2_bitshuffle_lz4(const std::vector<T>& vec, void* dst, size_t dst_size, int level, unsigned threads, int iterations)
{
	blosc2_set_nthreads(threads);
	blosc2_set_delta(0);
	blosc1_set_compressor("lz4");
	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);
	int res = 0;
	for (int i = 0; i < iterations; ++i)
		res = blosc2_compress(level, BLOSC_BITSHUFFLE, sizeof(typename std::vector<T>::value_type), vec.data(), bytes, dst, dst_size);
	if (res <= 0)
		return 0;
	return (size_t)res;
}

template<class... Args>
static inline std::string as_aligned_string(size_t width, const char* format, Args&&... args)
{
	char buffer[100];
	snprintf(buffer, sizeof(buffer), format, std::forward<Args>(args)...);

	auto len = strlen(buffer);
	if (len >= width)
		return std::string(buffer).substr(0, width);

	auto len2 = (width - len) / 2;
	std::string str(buffer);
	str.insert(0, len2, ' ');
	str.insert(str.size(), (width - len) - len2, ' ');
	return str;
}

template<class T>
struct Compress
{
	std::function<size_t(const std::vector<T>&, int, unsigned)> compress;
	std::string name;
	template<class Fun>
	Compress(Fun fun, const std::string& n)
	  : compress(fun)
	  , name(n)
	{
	}
};

template<class T>
void test_compression(const std::vector<T>& vec, const std::vector<Compress<T>>& functions, int level, unsigned threads)
{
	static constexpr size_t width = 20;

	size_t bytes = vec.size() * sizeof(typename std::vector<T>::value_type);

	// bench 
	std::cout << "|";
	for (const auto& c : functions) {
		auto st = msecs_since_epoch();
		auto s = c.compress(vec, level, threads);
		auto el = msecs_since_epoch() - st;
		double ratio = (double)bytes / (double)s;
		std::cout << as_aligned_string(width, "%d ms / %.2f", (int)el, ratio) << "|";
	}
	std::cout << std::endl;
}

template<class T>
void test_compression(const std::vector<T>& vec, unsigned threads)
{
	static constexpr size_t width = 20;

	std::vector<char> out(stenos_bound(vec.size() * sizeof(typename std::vector<T>::value_type)));
	

	std::vector<Compress<T>> functions;
	int iterations = iteration_count(vec);
	functions.push_back(Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_stenos(v, out.data(), out.size(), level, threads, iterations); }, "Stenos"));
	functions.push_back(Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_blosc2_shuffle(v, out.data(), out.size(), level, threads, iterations); }, "Blosc2 shuffle"));
	functions.push_back(Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_blosc2_delta_shuffle(v, out.data(), out.size(), level, threads, iterations); }, "Blosc2 delta"));
	functions.push_back(
	  Compress<T>([&](const auto& v, auto level, auto threads) { return compress_vec_blosc2_bitshuffle(v, out.data(), out.size(), level, threads, iterations); }, "Blosc2 bitshuffle"));

	// Print info
	std::cout << "Threads: " << threads << ", Iterations: " << iterations << std::endl;

	// print header
	std::cout << "|";
	for (const auto& c : functions)
		std::cout << as_aligned_string(width, "%s", c.name.c_str()) << "|";
	std::cout << std::endl;

	std::cout << "|";
	for (const auto& c : functions)
		std::cout << std::string(width, '-') << "|";
	std::cout << std::endl;

	// print benchmarks
	for (int level = 1; level <= 9; ++level) {
		test_compression(vec, functions, level, threads);
	}
}


template<class T>
void test_time_limited(const std::vector<T>& vec, unsigned threads)
{
	std::vector<char> dst(stenos_bound(vec.size() * sizeof(T)));
	size_t bytes = vec.size() * sizeof(T);

	stenos_timer_t* timer = stenos_make_timer();

	stenos_tick(timer);
	auto r1 = stenos_compress(vec.data(), sizeof(T), vec.size() * sizeof(T), dst.data(), dst.size(), 8);
	auto el_max = stenos_tock(timer);

	stenos_tick(timer);
	auto r2 = stenos_compress(vec.data(), sizeof(T), vec.size() * sizeof(T), dst.data(), dst.size(), 1);
	auto el_min = stenos_tock(timer);

	std::cout << "ratios: " << (double)bytes / r2 << " to " << (double)bytes / r1 << std::endl;

	// start at half the minimum time
	el_min /= 2;

	while (true) {
		auto ctx = stenos_make_context();
		if (el_min > el_max)
			el_min *= 1000;
		stenos_set_max_nanoseconds(ctx, el_min);

		stenos_tick(timer);
		auto r = stenos_compress_generic(ctx, vec.data(), sizeof(T), vec.size() * sizeof(T), dst.data(), dst.size());
		auto el = stenos_tock(timer);
		std::cout << "request " << el_min << " got " << el << " ratio " << (double)bytes / r << std::endl;
		if (el_min > el_max)
			break;
		el_min *= 2;
	}
}


template<class T>
std::string test_to_csv(const std::vector<T>& vec)
{
	std::ostringstream oss;
	oss.imbue(std::locale("de_DE.utf8"));

	oss << "sep=;" << std::endl;
	oss << "Level;StenosSpeed;StenosRatio;BloscZstdShuffleSpeed;BloscZstdShuffleRatio;BloscZstdBitshuffleSpeed;BloscZstdBitshuffleRatio;BloscLZ4ShuffleSpeed;BloscLZ4ShuffleRatio;BloscLZ4BitshuffleSpeed;BloscLZ4BitshuffleRatio" << std::endl;

	int iterations = iteration_count(vec)*5;

	std::vector<char> out(stenos_bound(vec.size() * sizeof(T)));

	std::vector<std::function<size_t(int)>> funs;
	funs.push_back([&](int level) { return compress_vec_stenos(vec, out.data(), out.size(), level, 1, iterations); });
	funs.push_back([&](int level) { return compress_vec_blosc2_shuffle_zstd(vec, out.data(), out.size(), level, 1, iterations); });
	funs.push_back([&](int level) { return compress_vec_blosc2_bitshuffle_zstd(vec, out.data(), out.size(), level, 1, iterations); });
	funs.push_back([&](int level) { return compress_vec_blosc2_shuffle_lz4(vec, out.data(), out.size(), level, 1, iterations); });
	funs.push_back([&](int level) { return compress_vec_blosc2_bitshuffle_lz4(vec, out.data(), out.size(), level, 1, iterations); });

	auto timer = stenos_make_timer();

	for (int level = 1; level <= 9; ++level) {
	
		oss << level << ";";
		for (size_t i = 0; i < funs.size(); ++i) {

			stenos_tick(timer);
			auto r = funs[i](level);
			auto el = stenos_tock(timer) * 1e-9;

			double ratio = (double)(vec.size() * sizeof(T)) / r;
			double speed = (double)(vec.size() * sizeof(T)) / (el  / iterations);

			oss << speed << ";" << ratio << ";";
		}
		oss << std::endl;
	}

	stenos_destroy_timer(timer);

	return oss.str();
}



// #include "stenos/cvector.hpp"
#include <future>
#include <cvector.hpp>
#include <array>


#include <stenos/cvector.hpp>
#include <stenos/timer.hpp>	

#include <iostream>
#include <algorithm>
#include <random>

int bench_img(int, char** const)
{
	{
		using namespace stenos;
		cvector<int> w;

		// fill with consecutive values
		for (size_t i = 0; i < 10000000; ++i)
			w.push_back((int)i);

		// very good compression ratio as data are sorted
		std::cout << "push_back: " << w.current_compression_ratio() << std::endl;

		// shuffle the cvector
		timer t;
		t.tick();
		std::mt19937 rng(0);
		std::shuffle(w.begin(), w.end(), rng);
		auto elapsed_ms = t.tock() * 1e-6;

		// Bad compression ratio on random values (but still better than 1)
		std::cout << "random_shuffle: " << w.current_compression_ratio() << " in " << elapsed_ms << " ms" << std::endl;

		// sort the cvector
		t.tick();
		std::sort(w.begin(), w.end());
		elapsed_ms = t.tock() * 1e-6;
		// Go back to original ratio
		std::cout << "sort: " << w.current_compression_ratio() << " in " << elapsed_ms << " ms" << std::endl;

		return 0;
	}
	{
		{
			


			//if (stenos_private_test_delta() != 0)
			//	throw std::runtime_error("");

			/* {
				std::ifstream fin("C:/Users/VM213788/Desktop/results.data",std::ios::binary);

				// create a cvector of char and fill it with provided file's content
				stenos::cvector<std::array<char,5>, 8, 1> myvec;
				
				myvec.assign(stenos::istreambuf_iterator<std::array<char, 5>>(fin), stenos::istreambuf_iterator<std::array<char, 5>>());

				std::cout << myvec.size() << " " << myvec.current_compression_ratio() << std::endl;
				return 0;
			}*/
			/* {
				std::vector<unsigned> vec(100000);
				for (size_t i = 0; i < vec.size(); ++i)
					vec[i] = i;

				std::vector<char> buffer(stenos_bound(vec.size() * 4));

				auto ctx = stenos_make_context();
				stenos_set_level(ctx, 9);
				stenos_set_block_size(ctx, 2);
				auto r = stenos_compress_generic(ctx, vec.data(), 4, vec.size() * 4, buffer.data(), buffer.size());

				stenos::cvector<unsigned, 2, 2> cvec;
				auto r2 = cvec.deserialize(buffer.data(), r);

				std::vector<unsigned> vec2(vec.size());
				std::copy(cvec.begin(), cvec.end(), vec2.begin());
				bool stop = true;
			}*/
			
			/* {
				std::random_device dev;
				std::mt19937 rng(dev());
				std::uniform_int_distribution<unsigned> dist(0, std::numeric_limits<unsigned>::max());
				stenos::cvector<unsigned, 1,2> vec;
				//vec.set_max_contexts(stenos::context_ratio(2, stenos::Ratio));
				std::vector<unsigned> truth;
				for (int i = 0; i < 1000000; ++i) {
					auto val = i;
					// dist(dev);
					vec.push_back(val);
					truth.push_back(val);
				}

				uint64_t st, el;


				st = msecs_since_epoch();
				for (int i = 0; i < 1000; ++i) {
					// printf("%i\n", i);
					vec.for_each(0, vec.size(), [](auto& val) { ++val; });
				}
				el = msecs_since_epoch() - st;
				std::cout << "for_each: " << el << std::endl;

				st = msecs_since_epoch();
#pragma omp parallel for
				for (int i = 0; i < 1000; ++i)
				{
					//printf("%i\n", i);
					vec.for_each(0, vec.size(), [](auto& val) { ++val; });
				}
				el = msecs_since_epoch() - st;
				std::cout << "parallel for_each: " << el << std::endl;
				

				st = msecs_since_epoch();
				std::shuffle(truth.begin(), truth.end(), std::random_device());
				el = msecs_since_epoch() - st;
				std::cout << el << " ms " << std::endl;

				st = msecs_since_epoch();
				std::sort(truth.begin(), truth.end());
				el = msecs_since_epoch() - st;
				std::cout << el << " ms " << std::endl;
				
				st = msecs_since_epoch();
				std::shuffle(vec.begin(), vec.end(), std::random_device());
				el = msecs_since_epoch() - st;
				std::cout << el << " ms " << vec.current_compression_ratio() << std::endl;

				st = msecs_since_epoch();
				std::sort(vec.begin(), vec.end()); 
				el = msecs_since_epoch() - st;
				std::cout << el << " ms " << vec.current_compression_ratio() << std::endl;

				std::cout << std::equal(truth.begin(), truth.end(), vec.begin()) << std::endl;


				//std::vector<char> buffer(stenos_bound(vec.size() * 4));
				std::ostringstream oss;
				size_t rr = vec.serialize(oss);

				std::vector<unsigned> vec2(vec.size());
				auto str = oss.str();
				size_t rr2 = stenos_decompress(str.data(), 4, rr, vec2.data(), vec2.size() * 4);
				bool stop = true;
				
				decltype(vec) vec3;
				std::istringstream iss(str);
				auto rr3 = vec3.deserialize(str.data(),str.size());
				memset(vec2.data(), 0, vec2.size() * 4);
				std::copy(vec3.begin(), vec3.end(), vec2.begin());
				stop = true;
			}*/

		}
		{
			//auto vec = (read_binary<12>("C:\\src\\stenos\\dataset\\1024_sq_float4.bin"));
			//auto vec = (read_binary<2>("C:\\src\\stenos\\dataset\\PI240_15s.wav"));
			auto vec = read_binary<4>("C:\\Users\\VM213788\\Desktop\\results4.data");
			//auto vec = (read_text<uint16_t>("C:\\src\\stenos\\dataset\\DIV.txt"));
			//auto vec = (read_text<double>("C:\\src\\stenos\\dataset\\UTOR.txt"));
			//auto vec = (read_text<uint8_t>("C:\\src\\stenos\\dataset\\tree_r.txt"));
			//auto vec = (read_text<uint8_t>("C:\\src\\stenos\\dataset\\javascript.js"));

		 

			
			/* auto timer = stenos_make_timer();
			blosc1_set_compressor("zstd");
			blosc2_set_nthreads(1);
			size_t bytesoftype = sizeof(decltype(vec)::value_type);
			std::vector<char> dst(vec.size() * bytesoftype);
			stenos_tick(timer);
			stenos_context_t* ctx = stenos_make_context();
			stenos_set_threads(ctx, 1);
			stenos_set_level(ctx, 5);
			size_t r = 0;
			for (int i=0; i < 10; ++i)
			 r = stenos_compress_generic(ctx, vec.data(), bytesoftype, vec.size() * bytesoftype, dst.data(), dst.size());
				// r = blosc2_compress(9, BLOSC_SHUFFLE, bytesoftype, vec.data(), vec.size() * bytesoftype, dst.data(), dst.size());
			auto el = stenos_tock(timer)*1e-6;
			printf("compress: %f B/s %f\n", (double)(vec.size() * bytesoftype) / (el / 1000.), (vec.size() * bytesoftype)/(double)r);
			//return 0;

			auto vec2 = vec;
			memset(vec2.data(), 0, vec2.size() * bytesoftype);
			stenos_tick(timer);
			size_t r2 = 0;
			for (int i = 0; i < 10; ++i)
			 r2 = stenos_decompress_generic(ctx, dst.data(), bytesoftype, r, vec2.data(), vec2.size() * bytesoftype);
			  //r2 = blosc2_decompress(dst.data(), r, vec2.data(), vec2.size() * bytesoftype);
			el = stenos_tock(timer) * 1e-6;
			printf("decompress: %f B/s\n", (double)(vec.size() * bytesoftype) / (el / 1000.));
			if (memcmp(vec.data(), vec2.data(), vec.size() * bytesoftype) != 0)
				throw std::runtime_error("");
			return 0;
			
			//auto vec = (read_text<double>("C:\\src\\stenos\\dataset\\SHYBPTOT.txt"));
			//auto vec = synthetic<19>(10000000);
			//auto vec = read_binary<1>("C:\\src\\stenos\\dataset\\javascript.js");
			blosc1_set_compressor("zstd");
			std::cout << "results.data" << std::endl;
			test_compression(vec, 1);
			return 0;
			*/
		}

		//auto vec = read_binary<12>("C:\\src\\stenos\\dataset\\953134_float3.bin");
		//auto vec = read_binary<16>("C:\\src\\stenos\\dataset\\232630_float4.bin");
		//auto vec = read_binary<16>("C:\\src\\stenos\\dataset\\1024_sq_float4.bin");
		//auto vec = read_binary<16>("C:\\src\\stenos\\dataset\\2048_sq_float4.bin");
		//stenos_private_test(vec.data(), vec.size() * sizeof(decltype(vec)::value_type));
		//auto vec = read_binary<4>("C:\\Users\\VM213788\\Desktop\\results4.data");
		//auto vec = read_text<uint16_t>("C:\\src\\stenos\\dataset\\WA.txt");
		//auto vec = read_text<uint16_t>("C:\\src\\stenos\\dataset\\LH1.txt");
		//auto vec = read_text<uint16_t>("C:\\src\\stenos\\dataset\\DIV.txt");
		auto vec = read_binary<2>("C:\\src\\stenos\\dataset\\2_PI240_15s.wav");
		//auto vec = read_text<double>("C:\\src\\stenos\\dataset\\UTOR.txt");
		//auto vec = read_text<double>("C:\\src\\stenos\\dataset\\SHYBPTOT.txt");
		//auto vec = read_text<uint8_t>("C:\\src\\stenos\\dataset\\javascript.js");
		//auto vec = read_binary<1>("C:\\src\\stenos\\dataset\\javascript.js");

		//test_time_limited(vec,1);
		//return 0;

		std::cout << test_to_csv(vec) << std::endl;
		return 0;

		blosc1_set_compressor("zstd");
		std::cout << "Blosc2 zstd" << std::endl;
		test_compression(vec, 1);
		return 0;
	}
	/* {

		unsigned val[256*3];
		for (int i = 0; i < 256 * 3; ++i)
			val[i] = rand();
		for (int i = 0; i < 48; ++i)
			val[i] = i & 15;
		for (int i = 256 * 3 - 48; i < 256 * 3; ++i)
			val[i] = i & 15;


		uint8_t dst[256 * 3 * 6];
		uint16_t buffer[256];
		uint8_t* dend = stenos::lz_compress_generic((uint8_t*)val, dst, 12, sizeof(dst), buffer);
		int ss = dend - dst;

		unsigned val2[512];
		STENOS_ASSERT_DEBUG(stenos::lz_decompress_generic(dst, (uint8_t*)val2, 12, ss) != nullptr,"");

		int ok = memcmp(val, val2, sizeof(val));
		bool stop = true;
	}*/
	/**/

	blosc2_init();
	blosc1_set_compressor("zstd");

	std::ifstream fin("C:\\src\\stenos\\dataset\\WA.txt");
	std::vector<unsigned short> img;
	while (fin) {
		unsigned short v;
		fin >> v;
		if (fin)
			img.push_back(v);
	}
	{
		std::ifstream fin("C:\\src\\stenos\\dataset\\LH1.txt");
		while (fin) {
			unsigned short v;
			fin >> v;
			if (fin)
				img.push_back(v);
		}
	}
	{
		std::ifstream fin("C:\\src\\stenos\\dataset\\DIV.txt");
		while (fin) {
			unsigned short v;
			fin >> v;
			if (fin)
				img.push_back(v);
		}
	}

	/*std::ifstream fin("C:/Users/VM213788/Desktop/tree_r.txt");
	std::ifstream fin2("C:/Users/VM213788/Desktop/tree_g.txt");
	std::ifstream fin3("C:/Users/VM213788/Desktop/tree_b.txt");
	struct RGB
	{
		uint8_t rgb[3];
	};
	std::vector<RGB> imgr;
	while (fin) {
		RGB v;
		fin >> v.rgb[0];
		fin2 >> v.rgb[1];
		fin3 >> v.rgb[2];
		if (fin)
			imgr.push_back(v);
	}
	auto img = imgr;
	*/
	/* std::ifstream fin("C:/Users/VM213788/Desktop/tree_g.txt");
	std::vector<uint8_t> img;
	while (fin) {
		uint8_t v;
		fin >> v;
		if (fin)
			img.push_back(v);
	}*/

	/* std::ifstream fin2("C:/Users/VM213788/Desktop/SHYBPTOT.txt");
	std::vector<double> img;
	while (fin2) {
		double v;
		fin2 >> v;
		if (fin2) {
			img.push_back(v);
		}
	}
	{
		std::ifstream fin("C:/Users/VM213788/Desktop/UTOR.txt");
		while (fin) {
			double v;
			fin >> v;
			if (fin) {
				img.push_back(v);
			}
		}
	}*/

	/* std::ifstream fin2("C:\\src\\stenos\\dataset\\232630_float4.bin", std::ios::binary);
	struct float4
	{
		float v[4];
	};
	std::vector<float4> img;
	while (fin2) {
		float4 v;
		fin2.read((char*)& v,sizeof(v));
		if (fin2) {
			img.push_back(v);
		}
	}*/

	/* std::ifstream fin2("C:\\Users\\VM213788\\Thermavip\\Qt6Widgets.dll", std::ios::binary);
	struct Char4
	{
		char data[1];
	};
	std::vector<char> img;

	while (fin2) {
		Char4 v;
		fin2.read((char*)&v, sizeof(v));
		if (fin2) {
			img.insert(img.end(), v.data,v.data+1);
		}
	}*/
	/*std::ifstream fin2("C:\\src\\stenos\\dataset\\image.jpg", std::ios::binary);
	std::vector<char> img;
	while (fin2) {
		char v;
		fin2.read((char*)&v, sizeof(v));
		if (fin2) {
			img.push_back(v);
		}
	}*/

	/* std::ifstream fin2("C:\\src\\stenos\\dataset\\953134_float3.bin", std::ios::binary);
	struct float3
	{
		float v[3];
	};
	std::vector<float3> img;
	while (fin2) {
		float3 v;
		fin2.read((char*)&v, sizeof(v));
		if (fin2) {
			img.push_back(v);
		}
	}*/

	// https://soundtags.wp.st-andrews.ac.uk/dtags/audio_compression/audio-samples/
	/* std::ifstream fin("C:/Users/VM213788/Desktop/PI240_15s.wav", std::ios::binary);
	unsigned FileTypeBlocID, FileSize, FileFormatID, FormatBlocID, BlocSize, Frequence, BytePerSec, DataBlocID, DataSize;
	uint16_t AudioFormat, NbrCanaux, BytePerBloc, BitsPerSample;
	fin.read((char*)&FileTypeBlocID, sizeof(FileTypeBlocID));
	fin.read((char*)&FileSize, sizeof(FileSize));
	fin.read((char*)&FileFormatID, sizeof(FileFormatID));
	fin.read((char*)&FormatBlocID, sizeof(FormatBlocID));
	fin.read((char*)&BlocSize, sizeof(BlocSize));
	fin.read((char*)&AudioFormat, sizeof(AudioFormat));
	fin.read((char*)&NbrCanaux, sizeof(NbrCanaux));
	fin.read((char*)&Frequence, sizeof(Frequence));
	fin.read((char*)&BytePerSec, sizeof(BytePerSec));
	fin.read((char*)&BytePerBloc, sizeof(BytePerBloc));
	fin.read((char*)&BitsPerSample, sizeof(BitsPerSample));
	fin.read((char*)&DataBlocID, sizeof(DataBlocID));
	fin.read((char*)&DataSize, sizeof(DataSize));
	std::vector<uint16_t> img;
	while (fin) {
		uint16_t v;
		fin.read((char*)&v, sizeof(v));
		if (fin)
			img.push_back(v);
	}*/
	// img.erase(img.begin(), img.begin() + 256);
	// img.resize(img.size() / 2);
	// img.erase(img.begin(), img.begin() + (img.size() / 256)*256);

	/* {
		int width = 640;		   // 640;
		int height = 512; // 512;
		using type = decltype(img)::value_type;
		static constexpr size_t bytesoftype = sizeof(decltype(img)::value_type);

		std::vector<char> dst(img.size() * bytesoftype);
		std::vector<char> dst2(img.size() * bytesoftype);
		auto st = msecs_since_epoch();
		size_t r = 0;
		for (int i=0; i < 1; ++i)
			r = stenos_image_compress(img.data(), bytesoftype, width, img.size() * bytesoftype, dst.data(), dst.size());

		//blosc_set_compressor("zstd");
		//r=blosc_compress(1, 0, 1, r, dst.data(), dst2.data(), dst2.size());

		auto el = msecs_since_epoch() - st;
		std::cout << (double)(img.size() * bytesoftype) / (double)r << " " << el << " ms"<< std::endl;


		st = msecs_since_epoch();
		charls::jpegls_encoder encoder;
		encoder.frame_info({ static_cast<uint32_t>(width), static_cast<uint32_t>(height), bytesoftype*8, 1 }).near_lossless(0).destination(dst.data(), dst.size());
		//.interleave_mode(charls::interleave_mode::sample);
		auto rr = encoder.encode(img.data(), img.size() * bytesoftype, width * bytesoftype);
		el = msecs_since_epoch() - st;
		std::cout << (double)(img.size() * bytesoftype) / (double)rr << " " << el << " ms" << std::endl;
	}*/

	/* for (int i = 0; i < 10; ++i) {
		auto tmp = img;
		img.insert(img.end(), tmp.begin(), tmp.end());
	}*/

	/* {
		auto img2 = img;
		zig_zag(img2.data(), 5600, 3200, img.data());
	}*/
	{
		blosc2_set_nthreads(1);

		unsigned typesize = sizeof(decltype(img)::value_type);
		std::vector<char> dst(stenos_bound(img.size() * typesize) * 2);

		auto st = msecs_since_epoch();
		size_t s = blosc2_compress(1, BLOSC_NOSHUFFLE, 1, img.data(), img.size() * typesize, dst.data(), dst.size());
		auto el = msecs_since_epoch() - st;
		std::cout << "RAW BLOSC: " << el << " ms " << ((double)img.size() * typesize) / (double)s << std::endl;

		for (int level = 0; level < 5; ++level) {

			st = msecs_since_epoch();
			s = ZSTD_compress(dst.data(), dst.size(), img.data(), img.size() * typesize, level);
			auto ms = msecs_since_epoch() - st;
			double rate = (double)(img.size() * typesize) / (ms / 1000.);
			std::cout << "RAW ZSTD " << level << ": " << ms << " ms " << (double)img.size() * typesize / (double)s << " rate(MB/s): " << rate / 1000000 << std::endl;
		}
		/* for (int i = 0; i < 10; ++i)
		{
			stenos::TimeConstraint cst;
			cst.nanoseconds = 250000000ull;
			cst.timer.tick();
			s = stenos_compress(img.data(), typesize, img.size() * typesize, dst.data(), dst.size(), stenos_options{(unsigned)i,1,0});
			auto ms = cst.timer.tock() / 1000000.;
			double rate = (double)(img.size() * typesize) / (ms / 1000.);
			std::cout << "ZSTD TIMER: " << ms << " ms " << (double)img.size() * typesize / (double)s << " rate(MB/s): " << rate / 1000000 << std::endl;
		}*/
		/*
		auto img2 = img;
		memset(img2.data(), 0, img.size() * typesize);
		st = msecs_since_epoch();
		auto r = stenos::parallel_decompress(1, img2.data(), img.size() * typesize, dst.data(), s);
		el = msecs_since_epoch() - st;
		std::cout << "decompress: " << el << " ms" << std::endl;

		if (memcmp(img.data(), img2.data(), img.size() * typesize) != 0)
			throw std::runtime_error("");
			*/
	}

	unsigned typesize = sizeof(decltype(img)::value_type);
	std::vector<decltype(img)::value_type> img2(img.size() * 2);
	std::vector<char> dst(stenos_bound(img.size() * typesize) * 2);
	size_t count = img.size();

	unsigned nthreads = 8;
	unsigned loopcount = 100;

	std::cout << (size_t)loopcount * count * (size_t)typesize << " bytes" << std::endl;

	{
		std::cout << "memcpy" << std::endl;
		auto st = msecs_since_epoch();
		unsigned s;
		for (unsigned i = 0; i < loopcount; ++i) {
			memcpy(dst.data(), img.data(), count * typesize);
			// s = blosc_compress(9, BLOSC_NOSHUFFLE, 1, s, dst.data(), img2.data(), img2.size());
		}
		auto el = msecs_since_epoch() - st;
		std::cout << el << " ms " << std::endl;
	}

	{

		std::cout << "blosc" << std::endl;
		blosc1_set_compressor("lz4");
		// blosc2_set_nthreads(8);

		auto st = msecs_since_epoch();
		unsigned s;
		for (unsigned i = 0; i < loopcount; ++i)
			s = blosc2_compress(1, BLOSC_SHUFFLE, typesize, img.data(), img.size() * typesize, dst.data(), dst.size());
		auto el = msecs_since_epoch() - st;

		std::cout << el << " ms " << (double)img.size() * typesize / (double)s << std::endl;

		st = msecs_since_epoch();
		blosc2_set_delta(1);
		for (unsigned i = 0; i < loopcount; ++i)
			s = blosc2_compress(1, BLOSC_SHUFFLE, typesize, img.data(), img.size() * typesize, dst.data(), dst.size());
		el = msecs_since_epoch() - st;

		std::cout << el << " ms " << (double)img.size() * typesize / (double)s << std::endl;

		st = msecs_since_epoch();
		blosc2_set_delta(0);
		for (unsigned i = 0; i < loopcount; ++i)
			s = blosc2_compress(1, BLOSC_BITSHUFFLE, typesize, img.data(), img.size() * typesize, dst.data(), dst.size());
		el = msecs_since_epoch() - st;
		std::cout << el << " ms " << (double)img.size() * typesize / (double)s << std::endl;

		st = msecs_since_epoch();
		for (unsigned i = 0; i < loopcount; ++i)
			s = blosc2_decompress(dst.data(), s, img2.data(), img2.size() * typesize);
		el = msecs_since_epoch() - st;
		std::cout << el << " ms " << std::endl;
	}

	{
		std::cout << "stenos" << std::endl;
		blosc1_set_compressor("zstd");
		uint64_t st, el;
		unsigned s = 0;

		for (unsigned level = 0; level < 10; ++level) {

			auto st = msecs_since_epoch();
			for (unsigned i = 0; i < loopcount; ++i) {
				//stenos_coptions opt{ level, 1, 0, 0 };
				s = stenos_compress(img.data(), typesize, count * typesize, dst.data(), dst.size(), level);
			}
			auto el = msecs_since_epoch() - st;

			// decompress
			auto img2 = img;
			memset(img2.data(), 0, img.size() * typesize);
			st = msecs_since_epoch();
			for (unsigned i = 0; i < loopcount; ++i) {
				stenos_decompress(dst.data(), typesize, s, img2.data(), count * typesize);
			}
			auto eld = msecs_since_epoch() - st;

			std::cout << el << " ms " << (double)(count * typesize) / (double)s << " " << eld << " ms" << std::endl;

			memset(img2.data(), 0, img.size() * typesize);
			stenos_decompress(dst.data(), typesize, s, img2.data(), count * typesize);
			bool ok = memcmp(img2.data(), img.data(), count * typesize) == 0;
			if (!ok)
				throw std::runtime_error("");
		}

		/* st = msecs_since_epoch();
		for (unsigned i = 0; i < loopcount; ++i)
			s = stenos_compress_parallel(img.data(), typesize, img.size() * typesize, dst.data(), dst.size(), 1, nthreads);
		el = msecs_since_epoch() - st;
		std::cout << nthreads << " " << el << " ms " << (double)img.size() * typesize / (double)s << std::endl;
		*/
		memset(img2.data(), 0, img2.size() * typesize);

		/* st = msecs_since_epoch();
		for (unsigned i = 0; i < loopcount; ++i)
			s = stenos_decompress(dst.data(), s, typesize, count * typesize, img2.data());
		el = msecs_since_epoch() - st;
		std::cout << el << " ms " << std::endl;
		*/

		return 0;
		size_t err_count = 0;
		for (size_t i = 0; i < count; ++i)
			if (memcmp(&img[i], &img2[i], typesize) != 0)
				++err_count;
		if (err_count)
			throw std::runtime_error("wrong decompression");
	}

	return 0;
}