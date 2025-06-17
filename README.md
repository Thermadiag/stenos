Stenos
Fast binary compression for C/C++
Stenos is a compression library primarily designed to compress binary structured data like arrays of integers or floating-point values. It serves the same purpose as the great blosc library, and borrowed its optimized shuffling routines. Stenos relies on zstd compression library.
As opposed to blosc, stenos does not need to be specified a filter like byte shuffling or bit shuffling. Instead, it tests several approaches and pick the most efficient compression among:
-	SIMD Block compression. This algorithm compresses blocks of 256 elements using a combination of bit packing, delta coding, RLE and fast LZ-like algorithm using SSE 4.1 and/or AVX2 instruction sets. This algorithm can compress at more or less 2GB/s and decompress at 3GB/s. ZSTD compression can then be applied on the residuals.
-	ZSTD compression on the shuffled (transposed) input (similar to blosc + zstd)
-	ZSTD compression on the shuffled + byte delta input
-	Direct ZSTD compression without shuffling.
Despite all these possibilities, stenos usually compress better and faster than blosc + zstd or lz4. The following graphs show the compression ratio versus speed for different types of data and testing stenos, blosc+zstd (byte shuffling + bit shuffling), blosc+lz4.

Stenos supports multi-threaded compression and decompression, but not (yet) streaming compression.
Usage
TODO
Compressed vector
Stenos library provides the C++ cvector class providing a compressed vector container with a similar interface to std::vector. See this documentation for more details.
Build
The following cmake options are available:
-	STENOS_ENABLE_AVX2(ON): force AVX2 support
-	STENOS_BUILD_ZSTD(OFF) : build zstd without trying to find it first
-	STENOS_BUILD_TESTS(ON): build the tests
-	STENOS_BUILD_BENCHS(ON): build the benchmarks
-	STENOS_NO_WARNINGS(OFF): treat warnings as errors
-	STENOS_BUILD_SHARED(ON): build shared version of stenos
-	STENOS_BUILD_STATIC(ON): build static version of stenos
If you link with the static version without using cmake, you must define STENOS_STATIC yourself.
