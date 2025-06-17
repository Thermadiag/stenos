/**
 * MIT License
 *
 * Copyright (c) 2025 Victor Moncada <vtr.moncada@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef STENOS_BITS_HPP
#define STENOS_BITS_HPP

#ifdef __clang__
// Get rid of VERY annoying and useless warnings (clang does not recognize some doxygen commands)
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif

/** @file */

/**\defgroup bits Bits: collection of functions for low level bits manipulation.

The bits module provides several portable low-level functions for bits manipulation:
	-	stenos::popcnt64: population count on a 64 bits word
	-	stenos::popcnt32: population count on a 32 bits word
	-	stenos::popcnt16: population count on a 16 bits word
	-	stenos::popcnt8: population count on a 8 bits word
	-	stenos::bit_scan_forward_32: index of the lowest set bit in a 32 bits word
	-	stenos::bit_scan_forward_64: index of the lowest set bit in a 64 bits word
	-	stenos::bit_scan_reverse_32: index of the highest set bit in a 32 bits word
	-	stenos::bit_scan_reverse_64: index of the highest set bit in a 32 bits word
	-	stenos::bit_scan_forward: index of the lowest set bit in a size_t word
	-	stenos::bit_scan_reverse: index of the highest set bit in a size_t word
	-	stenos::static_bit_scan_reverse: index of the highest set bit at compile time
	-	stenos::count_digits_base_10: number of digits to represent an integer in base 10
	-	stenos::nth_bit_set: index of the nth set bit in a 64 bits word
	-	stenos::byte_swap_16: byte swap for 16 bits word
	-	stenos::byte_swap_32: byte swap for 32 bits word
	-	stenos::byte_swap_64: byte swap for 64 bits word

See functions documentation for more details.
*/

/** \addtogroup bits
 *  @{
 */

/*#ifdef _MSC_VER
 // Silence msvc warning message about alignment
#define _ENABLE_EXTENDED_ALIGNED_STORAGE
#endif*/

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <type_traits>

#include "stenos_export.h"

#if defined(__APPLE__)
// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>
#endif

#if defined(__sun) || defined(sun)
#include <sys/byteorder.h>
#endif

#if defined(__FreeBSD__)
#include <sys/endian.h>
#endif

#if defined(__OpenBSD__)
#include <sys/types.h>
#endif

#if defined(__NetBSD__)
#include <sys/types.h>
#include <machine/bswap.h>
#endif

// From rapsody library
// https://stackoverflow.com/questions/4239993/determining-endianness-at-compile-time

#define STENOS_BYTEORDER_LITTLE_ENDIAN 0 // Little endian machine.
#define STENOS_BYTEORDER_BIG_ENDIAN 1	 // Big endian machine.

// Find byte order
#ifndef STENOS_BYTEORDER_ENDIAN
// Detect with GCC 4.6's macro.
#if defined(__BYTE_ORDER__)
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_LITTLE_ENDIAN
#elif (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_BIG_ENDIAN
#else
#error "Unknown machine byteorder endianness detected. User needs to define STENOS_BYTEORDER_ENDIAN."
#endif
// Detect with GLIBC's endian.h.
#elif defined(__GLIBC__)
#include <endian.h>
#if (__BYTE_ORDER == __LITTLE_ENDIAN)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_LITTLE_ENDIAN
#elif (__BYTE_ORDER == __BIG_ENDIAN)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_BIG_ENDIAN
#else
#error "Unknown machine byteorder endianness detected. User needs to define STENOS_BYTEORDER_ENDIAN."
#endif
// Detect with _LITTLE_ENDIAN and _BIG_ENDIAN macro.
#elif defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_LITTLE_ENDIAN
#elif defined(_BIG_ENDIAN) && !defined(_LITTLE_ENDIAN)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_BIG_ENDIAN
// Detect with architecture macros.
#elif defined(__sparc) || defined(__sparc__) || defined(_POWER) || defined(__powerpc__) || defined(__ppc__) || defined(__hpux) || defined(__hppa) || defined(_MIPSEB) || defined(_POWER) ||            \
  defined(__s390__)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_BIG_ENDIAN
#elif defined(__i386__) || defined(__alpha__) || defined(__ia64) || defined(__ia64__) || defined(_M_IX86) || defined(_M_IA64) || defined(_M_ALPHA) || defined(__amd64) || defined(__amd64__) ||        \
  defined(_M_AMD64) || defined(__x86_64) || defined(__x86_64__) || defined(_M_X64) || defined(__bfin__)
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_LITTLE_ENDIAN
#elif defined(_MSC_VER) && (defined(_M_ARM) || defined(_M_ARM64))
#define STENOS_BYTEORDER_ENDIAN STENOS_BYTEORDER_LITTLE_ENDIAN
#else
#error "Unknown machine byteorder endianness detected. User needs to define STENOS_BYTEORDER_ENDIAN."
#endif
#endif

// Find 32/64 bits
#if defined(__x86_64__) || defined(__ppc64__) || defined(_WIN64)
#define STENOS_ARCH_64
#else
#define STENOS_ARCH_32
#endif

// BIM2 instruction set is not properly defined on msvc
#if defined(_MSC_VER) && defined(__AVX2__)
#ifndef __BMI2__
#define __BMI2__
#endif
#endif

// __MINGW32__ doesn't seem to be properly defined, so define it.
#ifndef __MINGW32__
#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32)) && defined(__GNUC__) && !defined(__CYGWIN__)
#define __MINGW32__
#endif
#endif

// Check for C++17
#if defined(_MSC_VER) && !defined(__clang__)
#if _MSVC_LANG >= 201703L
#define STENOS_HAS_CPP_17
#endif
#if _MSVC_LANG >= 202002L
#define STENOS_HAS_CPP_20
#endif
#else
#if __cplusplus >= 201703L
#define STENOS_HAS_CPP_17
#endif
#if __cplusplus >= 202002L
#define STENOS_HAS_CPP_20
#endif
#endif

// If constexpr
#ifdef STENOS_HAS_CPP_17
#define STENOS_CONSTEXPR constexpr
#else
#define STENOS_CONSTEXPR
#endif

// Unreachable code
#ifdef _MSC_VER
#define STENOS_UNREACHABLE() __assume(0)
#else
#define STENOS_UNREACHABLE() __builtin_unreachable()
#endif

// pragma directive might be different between compilers, so define a generic STENOS_PRAGMA macro.
// Use STENOS_PRAGMA with no quotes around argument (ex: STENOS_PRAGMA(omp parallel) and not STENOS_PRAGMA("omp parallel") ).
#if defined(_MSC_VER) && !defined(__clang__)
#define STENOS_INTERNAL_PRAGMA(text) __pragma(text)
#else
#define STENOS_INTERNAL_PRAGMA(text) _Pragma(#text)
#endif
#define STENOS_PRAGMA(text) STENOS_INTERNAL_PRAGMA(text)

// no inline
#if defined(_MSC_VER) && !defined(__clang__)
#define STENOS_NOINLINE(...) __declspec(noinline) __VA_ARGS__
#else
#define STENOS_NOINLINE(...) __VA_ARGS__ __attribute__((noinline))
#endif

// For msvc, define __SSE__ and __SSE2__ manually
#if !defined(__SSE2__) && defined(_MSC_VER) && (defined(_M_X64) || _M_IX86_FP >= 2)
#define __SSE__ 1
#define __SSE2__ 1
#endif

// prefetching
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#define STENOS_PREFETCH(p) __builtin_prefetch(reinterpret_cast<const char*>(p))
#elif defined(__SSE2__)
#define STENOS_PREFETCH(p) _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T0)
#else
#define STENOS_PREFETCH(p)
#endif

// SSE intrinsics
#if defined(__SSE2__)
#if defined(__unix) || defined(__linux) || defined(__posix)
#include <emmintrin.h>
#include <xmmintrin.h>
#else
#include <emmintrin.h>
#include <xmmintrin.h>
#endif

#endif

// fallthrough
#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0
#endif
#if __has_cpp_attribute(clang::fallthrough)
#define STENOS_FALLTHROUGH() [[clang::fallthrough]]
#elif __has_cpp_attribute(gnu::fallthrough)
#define STENOS_FALLTHROUGH() [[gnu::fallthrough]]
#else
#define STENOS_FALLTHROUGH()
#endif

// likely/unlikely definition
#if !defined(_MSC_VER) || defined(__clang__)
#define STENOS_LIKELY(...) (__builtin_expect(!!(__VA_ARGS__), 1))
#define STENOS_UNLIKELY(...) (__builtin_expect(!!(__VA_ARGS__), 0))
#else

#ifdef STENOS_HAS_CPP_20
#define STENOS_LIKELY(...) (__VA_ARGS__) [[likely]]
#define STENOS_UNLIKELY(...) (__VA_ARGS__) [[unlikely]]
#else
#define STENOS_LIKELY(...) (__VA_ARGS__)
#define STENOS_UNLIKELY(...) (__VA_ARGS__)
#endif
#endif

// Simple function inlining
#define STENOS_INLINE inline

// Strongest available function inlining
#if (defined(__GNUC__) && (__GNUC__ >= 4)) || defined(__clang__)
#define STENOS_ALWAYS_INLINE __attribute__((always_inline)) inline
#define STENOS_EXTENSION __extension__
#define STENOS_HAS_ALWAYS_INLINE
#elif defined(__GNUC__)
#define STENOS_ALWAYS_INLINE inline
#define STENOS_EXTENSION __extension__
#elif (defined _MSC_VER) || (defined __INTEL_COMPILER)
#define STENOS_HAS_ALWAYS_INLINE
#define STENOS_ALWAYS_INLINE __forceinline
#else
#define STENOS_ALWAYS_INLINE inline
#endif

#ifndef STENOS_EXTENSION
#define STENOS_EXTENSION
#endif

// assume data are aligned
#if defined(__GNUC__) && (__GNUC__ >= 4 && __GNUC_MINOR__ >= 7)
#define STENOS_RESTRICT __restrict
#define STENOS_ASSUME_ALIGNED(type, ptr, out, alignment) type* STENOS_RESTRICT out = (type*)__builtin_assume_aligned((ptr), alignment);
#elif defined(__GNUC__)
#define STENOS_RESTRICT __restrict
#define STENOS_ASSUME_ALIGNED(type, ptr, out, alignment) type* STENOS_RESTRICT out = (ptr);
// on intel compiler, another way is to use #pragma vector aligned before the loop.
#elif defined(__INTEL_COMPILER) || defined(__ICL) || defined(__ICC) || defined(__ECC)
#define STENOS_RESTRICT restrict
#define STENOS_ASSUME_ALIGNED(type, ptr, out, alignment)                                                                                                                                               \
	type* STENOS_RESTRICT out = ptr;                                                                                                                                                               \
	__assume_aligned(out, alignment);
#elif defined(__IBMCPP__)
#define STENOS_RESTRICT restrict
#define STENOS_ASSUME_ALIGNED(type, ptr, out, alignment) type __attribute__((aligned(alignment)))* STENOS_RESTRICT out = (type __attribute__((aligned(alignment)))*)(ptr);
#elif defined(_MSC_VER)
#define STENOS_RESTRICT __restrict
#define STENOS_ASSUME_ALIGNED(type, ptr, out, alignment) type* STENOS_RESTRICT out = ptr;
#endif

// Forces data to be n-byte aligned (this might be used to satisfy SIMD requirements).
#if (defined __GNUC__) || (defined __PGI) || (defined __IBMCPP__) || (defined __ARMCC_VERSION) || (defined __clang__)
#define STENOS_ALIGN_TO_BOUNDARY(n) __attribute__((aligned(n)))
#elif (defined _MSC_VER)
#define STENOS_ALIGN_TO_BOUNDARY(n) __declspec(align(n))
#elif (defined __SUNPRO_CC)
// FIXME not sure about this one:
#define STENOS_ALIGN_TO_BOUNDARY(n) __attribute__((aligned(n)))
#else
#define STENOS_ALIGN_TO_BOUNDARY(n) STENOS_USER_ALIGN_TO_BOUNDARY(n)
#endif

#ifndef STENOS_DEBUG
#ifndef NDEBUG
#define STENOS_DEBUG
#endif
#endif

// Debug assertion
#ifndef STENOS_DEBUG
#define STENOS_ASSERT_DEBUG(condition, msg)
#else
#define STENOS_ASSERT_DEBUG(condition, ...) assert((condition) && (__VA_ARGS__))
#endif

// Abort program with a last message
#define STENOS_ABORT(...)                                                                                                                                                                              \
	{                                                                                                                                                                                              \
		printf(__VA_ARGS__);                                                                                                                                                                   \
		fflush(stdout);                                                                                                                                                                        \
		abort();                                                                                                                                                                               \
	}

// Support for __has_builtin
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

// Support for __has_attribute
#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

// Check for aligned memory allocation functions
#if ((defined __QNXNTO__) || (defined _GNU_SOURCE) || ((defined _XOPEN_SOURCE) && (_XOPEN_SOURCE >= 600))) && (defined _POSIX_ADVISORY_INFO) && (_POSIX_ADVISORY_INFO > 0)
#define STENOS_HAS_POSIX_MEMALIGN 1
#else
#define STENOS_HAS_POSIX_MEMALIGN 0
#endif

#if defined(__INTEL_COMPILER) || defined(__ICL) || defined(__ICC) || defined(__ECC)
#define STENOS_HAS_MM_MALLOC 1
#else
#define STENOS_HAS_MM_MALLOC 0
#endif

#if defined(_MSC_VER) && (!defined(_WIN32_WCE))
#define STENOS_HAS_ALIGNED_MALLOC 1
#else
#define STENOS_HAS_ALIGNED_MALLOC 0
#endif

#if defined(__MINGW32__) || defined(__MINGW64_VERSION_MAJOR)
#define STENOS_HAS_MINGW_ALIGNED_MALLOC 1
#else
#define STENOS_HAS_MINGW_ALIGNED_MALLOC 0
#endif

#if defined(STENOS_HAS_POSIX_MEMALIGN) || defined(STENOS_HAS_MM_MALLOC) || defined(STENOS_HAS_ALIGNED_MALLOC) || defined(STENOS_HAS_MINGW_ALIGNED_MALLOC)
#define STENOS_HAS_ALIGNED_ALLOCATION
#endif

#if defined(__clang__)

#define STENOS_COMPARE_FLOAT(...) __pragma(clang diagnostic push) __pragma(clang diagnostic ignored "-Wfloat-equal") __VA_ARGS__ __pragma(clang diagnostic pop)

#else
#define STENOS_COMPARE_FLOAT(...) __VA_ARGS__
#endif

#if defined(__clang__)

#define STENOS_PROTOTYPE(...) __pragma(clang diagnostic push) __pragma(clang diagnostic ignored "-Wmissing-prototypes") __VA_ARGS__ __pragma(clang diagnostic pop)

#else
#define STENOS_PROTOTYPE(...) __VA_ARGS__
#endif

namespace stenos
{

	namespace detail
	{

		/// Like malloc, but the returned pointer is guaranteed to be alignment-byte aligned.
		/// Fast, but wastes alignment additional bytes of memory. Does not throw any exception.
		inline auto handmade_aligned_malloc(size_t size, size_t alignment) -> void*
		{
			void* ptr = nullptr;
			alignment--;

			size_t offset = 0;
			std::uint8_t* mem = nullptr;

			// Room for padding and extra pointer stored in front of allocated area
			size_t overhead = alignment + sizeof(void*);

			// Avoid integer overflow
			if (size > (SIZE_MAX - overhead)) {
				return nullptr;
			}

			mem = static_cast<std::uint8_t*>(malloc(size + overhead));
			if (mem == nullptr) {
				return mem;
			}

			// Use the fact that alignment + 1U is a power of 2
			offset = ((alignment ^ (reinterpret_cast<std::uintptr_t>(mem + sizeof(void*)) & alignment)) + 1U) & alignment;
			ptr = static_cast<void*>(mem + sizeof(void*) + offset);
			(static_cast<void**>(ptr))[-1] = mem;
			return ptr;
		}

		/// Frees memory allocated with handmade_aligned_malloc
		inline void handmade_aligned_free(void* ptr)
		{
			// Generic implementation has malloced pointer stored in front of used area
			if (ptr != nullptr) {
				free((static_cast<void**>(ptr))[-1]);
			}
		}

	}

	/// @brief  Allocates \a size bytes. The returned pointer is guaranteed to have \a align bytes alignment.
	/// @param size size in bytes to allocate
	/// @param align alignment of result pointer
	/// @return algned pointer or NULL on error
	inline auto aligned_malloc(size_t size, size_t align) -> void*
	{
		void* result = nullptr;

#if STENOS_HAS_POSIX_MEMALIGN
		if (posix_memalign(&result, align, size))
			result = 0;
#elif STENOS_HAS_MM_MALLOC
		result = _mm_malloc(size, align);
#elif STENOS_HAS_ALIGNED_MALLOC
		result = _aligned_malloc(size, align);
#elif STENOS_HAS_MINGW_ALIGNED_MALLOC
		result = __mingw_aligned_malloc(size, align);
#else
		result = detail::handmade_aligned_malloc(size, align);
#endif
		return result;
	}

	/// @brief Frees memory allocated with aligned_malloc.
	inline void aligned_free(void* ptr)
	{
#if STENOS_HAS_POSIX_MEMALIGN
		free(ptr);
#elif STENOS_HAS_MM_MALLOC
		_mm_free(ptr);
#elif STENOS_HAS_ALIGNED_MALLOC
		_aligned_free(ptr);
#elif STENOS_HAS_MINGW_ALIGNED_MALLOC
		__mingw_aligned_free(ptr);
#else
		detail::handmade_aligned_free(ptr);
#endif
	}

} // end namespace stenos

#if defined(__SIZEOF_INT128__)

namespace stenos
{
	STENOS_ALWAYS_INLINE void umul128(const uint64_t m1, const uint64_t m2, uint64_t* const rl, uint64_t* const rh)
	{
		const unsigned __int128 r = static_cast<unsigned __int128>(m1) * m2;

		*rh = static_cast<uint64_t>(r >> 64);
		*rl = static_cast<uint64_t>(r);
	}
}
#define STENOS_HAS_FAST_UMUL128 1

#elif (defined(__IBMC__) || defined(__IBMCPP__)) && defined(__LP64__)

namespace stenos
{
	STENOS_ALWAYS_INLINE void umul128(const uint64_t m1, const uint64_t m2, uint64_t* const rl, uint64_t* const rh)
	{
		*rh = __mulhdu(m1, m2);
		*rl = m1 * m2;
	}
}
#define STENOS_HAS_FAST_UMUL128 1

#elif defined(_MSC_VER) && (defined(_M_ARM64) || (defined(_M_X64) && defined(__INTEL_COMPILER)))

#include <intrin.h>

namespace stenos
{
	STENOS_ALWAYS_INLINE void umul128(const uint64_t m1, const uint64_t m2, uint64_t* const rl, uint64_t* const rh)
	{
		*rh = __umulh(m1, m2);
		*rl = m1 * m2;
	}
}
#define STENOS_HAS_FAST_UMUL128 1

#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IA64))

#include <intrin.h>
#pragma intrinsic(_umul128)

namespace stenos
{
	static STENOS_ALWAYS_INLINE void umul128(const uint64_t m1, const uint64_t m2, uint64_t* const rl, uint64_t* const rh)
	{
		*rl = _umul128(m1, m2, rh);
	}
}
#define STENOS_HAS_FAST_UMUL128 1

#else // defined( _MSC_VER )

// _umul128() code for 32-bit systems, adapted from Hacker's Delight,
// Henry S. Warren, Jr.

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)

#include <intrin.h>
#pragma intrinsic(__emulu)
#define __STENOS_EMULU(x, y) __emulu(x, y)

#else // defined( _MSC_VER ) && !defined( __INTEL_COMPILER )

#define __STENOS_EMULU(x, y) ((uint64_t)(x) * (y))

#endif // defined( _MSC_VER ) && !defined( __INTEL_COMPILER )

namespace stenos
{
	static inline void umul128(const uint64_t u, const uint64_t v, uint64_t* const rl, uint64_t* const rh)
	{
		*rl = u * v;

		const uint32_t u0 = static_cast<uint32_t>(u);
		const uint32_t v0 = static_cast<uint32_t>(v);
		const uint64_t w0 = __STENOS_EMULU(u0, v0);
		const uint32_t u1 = static_cast<uint32_t>(u >> 32);
		const uint32_t v1 = static_cast<uint32_t>(v >> 32);
		const uint64_t t = __STENOS_EMULU(u1, v0) + static_cast<uint32_t>(w0 >> 32);
		const uint64_t w1 = __STENOS_EMULU(u0, v1) + static_cast<uint32_t>(t);

		*rh = __STENOS_EMULU(u1, v1) + static_cast<uint32_t>(w1 >> 32) + static_cast<uint32_t>(t >> 32);
	}
}

#endif

#ifdef __GNUC__
#define GNUC_PREREQ(x, y) (__GNUC__ > x || (__GNUC__ == x && __GNUC_MINOR__ >= y))
#else
#define GNUC_PREREQ(x, y) 0
#endif

#ifdef __clang__
#define CLANG_PREREQ(x, y) (__clang_major__ > (x) || (__clang_major__ == (x) && __clang_minor__ >= (y)))
#else
#define CLANG_PREREQ(x, y) 0
#endif

#if (_MSC_VER < 1900) && !defined(__cplusplus)
#define inline __inline
#endif

#if (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
#define X86_OR_X64
#endif

#if GNUC_PREREQ(4, 2) || __has_builtin(__builtin_popcount)
#define HAVE_BUILTIN_POPCOUNT
#endif

#if GNUC_PREREQ(4, 2) || CLANG_PREREQ(3, 0)
#define HAVE_ASM_POPCNT
#endif

#if defined(X86_OR_X64) && (defined(HAVE_ASM_POPCNT) || defined(_MSC_VER))
#define HAVE_POPCNT
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <immintrin.h>
#include <intrin.h>
#endif

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <nmmintrin.h>
#endif

namespace stenos
{
	namespace detail
	{
		// Define default popcount functions

		// This uses fewer arithmetic operations than any other known
		// implementation on machines with fast multiplication.
		// It uses 12 arithmetic operations, one of which is a multiply.
		// http://en.wikipedia.org/wiki/Hamming_weight#Efficient_implementation
		STENOS_ALWAYS_INLINE auto popcount64(std::uint64_t x) -> unsigned
		{
			std::uint64_t m1 = 0x5555555555555555LL;
			std::uint64_t m2 = 0x3333333333333333LL;
			std::uint64_t m4 = 0x0F0F0F0F0F0F0F0FLL;
			std::uint64_t h01 = 0x0101010101010101LL;

			x -= (x >> 1) & m1;
			x = (x & m2) + ((x >> 2) & m2);
			x = (x + (x >> 4)) & m4;

			return (x * h01) >> 56;
		}
		STENOS_ALWAYS_INLINE auto popcount32(uint32_t i) -> unsigned
		{
			i = i - ((i >> 1) & 0x55555555);		// add pairs of bits
			i = (i & 0x33333333) + ((i >> 2) & 0x33333333); // quads
			i = (i + (i >> 4)) & 0x0F0F0F0F;		// groups of 8
			return (i * 0x01010101) >> 24;			// horizontal sum of bytes
		}
	}

#if defined(HAVE_ASM_POPCNT) && defined(__x86_64__)

#define STENOS_HAS_ASM_POPCNT

	STENOS_ALWAYS_INLINE auto popcnt64(std::uint64_t x) -> unsigned
	{
		__asm__("popcnt %1, %0" : "=r"(x) : "0"(x));
		return static_cast<unsigned>(x);
	}

	STENOS_ALWAYS_INLINE auto popcnt32(uint32_t x) -> unsigned
	{
		return detail::popcount32(x);
	}

#elif defined(HAVE_ASM_POPCNT) && defined(__i386__)

#define STENOS_HAS_ASM_POPCNT

	STENOS_ALWAYS_INLINE unsigned popcnt32(uint32_t x)
	{
		__asm__("popcnt %1, %0" : "=r"(x) : "0"(x));
		return x;
	}

	STENOS_ALWAYS_INLINE unsigned popcnt64(std::uint64_t x)
	{
		return popcnt32((uint32_t)x) + popcnt32((uint32_t)(x >> 32));
	}

#elif defined(_MSC_VER) && defined(_M_X64)

#define STENOS_HAS_BUILTIN_POPCNT

	STENOS_ALWAYS_INLINE unsigned popcnt64(std::uint64_t x)
	{
		return (unsigned)_mm_popcnt_u64(x);
	}

	STENOS_ALWAYS_INLINE unsigned popcnt32(uint32_t x)
	{
		return (unsigned)_mm_popcnt_u32(x);
	}

#elif defined(_MSC_VER) && defined(_M_IX86)

#define STENOS_HAS_BUILTIN_POPCNT

	STENOS_ALWAYS_INLINE unsigned popcnt64(std::uint64_t x)
	{
		return _mm_popcnt_u32((uint32_t)x) + _mm_popcnt_u32((uint32_t)(x >> 32));
	}
	STENOS_ALWAYS_INLINE unsigned popcnt32(uint32_t x)
	{
		return _mm_popcnt_u32(x);
	}

	/* non x86 CPUs */
#elif defined(HAVE_BUILTIN_POPCOUNT)

#define STENOS_HAS_BUILTIN_POPCNT

	STENOS_ALWAYS_INLINE std::uint64_t popcnt64(std::uint64_t x)
	{
		return __builtin_popcountll(x);
	}
	STENOS_ALWAYS_INLINE uint32_t popcnt32(uint32_t x)
	{
		return __builtin_popcount(x);
	}

	/* no hardware POPCNT,
	 * use pure integer algorithm */
#else

	STENOS_ALWAYS_INLINE std::uint64_t popcnt64(std::uint64_t x)
	{
		return detail::popcount64(x);
	}
	STENOS_ALWAYS_INLINE uint32_t popcnt32(uint32_t x)
	{
		return detail::popcount32(x);
	}

#endif

	STENOS_ALWAYS_INLINE auto popcnt8(unsigned char value) -> unsigned
	{
		static const unsigned char ones[256] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3,
							 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4,
							 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1,
							 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5,
							 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
							 6, 4, 5, 5, 6, 5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8 };
		return ones[value];
	}
	STENOS_ALWAYS_INLINE auto popcnt16(unsigned short value) -> unsigned
	{
#ifdef _MSC_VER
		return __popcnt16(value);
#else
		return popcnt8(value & 0xFF) + popcnt8(value >> 8);
#endif
	}

	///
	/// @function unsigned popcnt16(unsigned short value)
	/// @brief Returns the number of set bits in \a value.
	///

	///
	/// @function unsigned popcnt32(unsigned int value)
	/// @brief Returns the number of set bits in \a value.
	///

	///
	/// @function unsigned popcnt64(unsigned long long value)
	/// @brief Returns the number of set bits in \a value.
	///

#if defined(_MSC_VER) || ((defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))))
#define STENOS_HAS_BUILTIN_BITSCAN
#endif

	STENOS_ALWAYS_INLINE auto bit_scan_forward_8(std::uint8_t val) -> unsigned int
	{
		static const std::uint8_t scan_forward_8[] = { 8, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1,
							       0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0,
							       1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7,
							       0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0,
							       2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1,
							       0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0 };
		STENOS_PREFETCH(scan_forward_8);
		return scan_forward_8[val];
	}
	STENOS_ALWAYS_INLINE auto bit_scan_reverse_8(std::uint8_t val) -> unsigned int
	{
		static const std::uint8_t scan_reverse_8[] = { 8, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
							       5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
							       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7,
							       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
							       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
							       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 };
		return scan_reverse_8[val];
	}

	/// @brief Returns the lowest set bit index in \a val
	/// Undefined if val==0.
	STENOS_ALWAYS_INLINE auto bit_scan_forward_32(std::uint32_t val) -> unsigned int
	{
#if defined(_MSC_VER) /* Visual */
		unsigned long r = 0;
		_BitScanForward(&r, val);
		return static_cast<unsigned>(r);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) /* Use GCC Intrinsic */
		return __builtin_ctz(val);
#else								     /* Software version */
		static const int MultiplyDeBruijnBitPosition[32] = { 0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9 };
		return MultiplyDeBruijnBitPosition[((uint32_t)((val & -val) * 0x077CB531U)) >> 27];
#endif
	}

	/// @brief Returns the highest set bit index in \a val
	/// Undefined if val==0.
	STENOS_ALWAYS_INLINE auto bit_scan_reverse_32(std::uint32_t val) -> unsigned int
	{
#if defined(_MSC_VER) /* Visual */
		unsigned long r = 0;
		_BitScanReverse(&r, val);
		return static_cast<unsigned>(r);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) /* Use GCC Intrinsic */
		return 31 - __builtin_clz(val);
#else								     /* Software version */
		static const unsigned int pos[32] = { 0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9 };
		// does not work for 0
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v = (v >> 1) + 1;
		return pos[(v * 0x077CB531UL) >> 27];
#endif
	}

	/// @brief Returns the lowest set bit index in \a bb.
	/// Developed by Kim Walisch (2012).
	/// Undefined if bb==0.
	STENOS_ALWAYS_INLINE auto bit_scan_forward_64(std::uint64_t bb) noexcept -> unsigned
	{
#if defined(_MSC_VER) && defined(_WIN64)
		unsigned long r = 0;
		_BitScanForward64(&r, bb);
		return static_cast<unsigned>(r);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3)))
		return __builtin_ctzll(bb);
#else
		static const unsigned forward_index64[64] = { 0,  47, 1,  56, 48, 27, 2,  60, 57, 49, 41, 37, 28, 16, 3,  61, 54, 58, 35, 52, 50, 42, 21, 44, 38, 32, 29, 23, 17, 11, 4, 62,
							      46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43, 31, 22, 10, 45, 25, 39, 14, 33, 19, 30, 9,  24, 13, 18, 8,  12, 7,  6,  5, 63 };
		const std::uint64_t debruijn64 = std::int64_t(0x03f79d71b4cb0a89);
		return forward_index64[((bb ^ (bb - 1)) * debruijn64) >> 58];
#endif
	}

	/// @brief Returns the highest set bit index in \a bb.
	/// Developed by Kim Walisch, Mark Dickinson.
	/// Undefined if bb==0.
	STENOS_ALWAYS_INLINE auto bit_scan_reverse_64(std::uint64_t bb) noexcept -> unsigned
	{
#if (defined(_MSC_VER) && defined(_WIN64)) //|| defined(__MINGW64_VERSION_MAJOR)
		unsigned long r = 0;
		_BitScanReverse64(&r, bb);
		return static_cast<unsigned>(r);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3)))
		return 63 - __builtin_clzll(bb);
#else
		static const unsigned backward_index64[64] = { 0,  47, 1,  56, 48, 27, 2,  60, 57, 49, 41, 37, 28, 16, 3,  61, 54, 58, 35, 52, 50, 42, 21, 44, 38, 32, 29, 23, 17, 11, 4, 62,
							       46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43, 31, 22, 10, 45, 25, 39, 14, 33, 19, 30, 9,  24, 13, 18, 8,  12, 7,  6,  5, 63 };
		const std::uint64_t debruijn64 = std::int64_t(0x03f79d71b4cb0a89);
		// assert(bb != 0);
		bb |= bb >> 1;
		bb |= bb >> 2;
		bb |= bb >> 4;
		bb |= bb >> 8;
		bb |= bb >> 16;
		bb |= bb >> 32;
		return backward_index64[(bb * debruijn64) >> 58];
#endif
	}

	/// @brief Returns the lowest set bit index in \a bb.
	/// Undefined if bb==0.
	STENOS_ALWAYS_INLINE auto bit_scan_forward(size_t bb) noexcept -> unsigned
	{
#ifdef STENOS_ARCH_64
		return bit_scan_forward_64(bb);
#else
		return bit_scan_forward_32(bb);
#endif
	}

	/// @brief Returns the highest set bit index in \a bb.
	/// Undefined if bb==0.
	STENOS_ALWAYS_INLINE auto bit_scan_reverse(size_t bb) noexcept -> unsigned
	{
#ifdef STENOS_ARCH_64
		return bit_scan_reverse_64(bb);
#else
		return bit_scan_reverse_32(bb);
#endif
	}

#if defined(_MSC_VER) || ((defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))))
#define STENOS_HAS_BUILTIN_BITSCAN
#endif

	/// @brief Returns a byte-swapped representation of the 16-bit argument.
	STENOS_ALWAYS_INLINE auto byte_swap_16(std::uint16_t value) -> std::uint16_t
	{
#if defined(_MSC_VER) && !defined(_DEBUG)
#if defined __clang__ // clang-cl
		return __builtin_bswap16(value);
#else
		return _byteswap_ushort(value);
#endif
#else
		return static_cast<std::uint16_t>((value << 8U) | (value >> 8U));
#endif
	}

#if (defined(__GNUC__) && !defined(__ICC)) || defined(__APPLE__) || defined(__sun) || defined(sun) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) ||                           \
  (defined(_MSC_VER) && !defined(_DEBUG))
#define STENOS_HAS_BUILTIN_BYTESWAP
#endif

	/// @brief Returns a byte-swapped representation of the 32-bit argument.
	STENOS_ALWAYS_INLINE auto byte_swap_32(std::uint32_t value) -> std::uint32_t
	{
#if defined(__GNUC__) && !defined(__ICC)
		return __builtin_bswap32(value);
#elif defined(__APPLE__)
		return OSSwapInt32(value);
#elif defined(__sun) || defined(sun)
		return BSWAP_32(value);
#elif defined(__FreeBSD__)
		return bswap32(value);
#elif defined(__OpenBSD__)
		return swap32(value);
#elif defined(__NetBSD__)
		return bswap32(value);
#elif defined(_MSC_VER) && !defined(_DEBUG)
#if defined __clang__ // clang-cl
		return __builtin_bswap32(value);
#else
		return _byteswap_ulong(value);
#endif
#else
		return ((((value) & 0xff000000) >> 24) | (((value) & 0x00ff0000) >> 8) | (((value) & 0x0000ff00) << 8) | (((value) & 0x000000ff) << 24));
#endif
	}

	/// @brief Returns a byte-swapped representation of the 64-bit argument.
	STENOS_ALWAYS_INLINE auto byte_swap_64(std::uint64_t value) -> std::uint64_t
	{
#if defined(__GNUC__) && !defined(__ICC)
		return __builtin_bswap64(value);
#elif defined(__APPLE__)
		return OSSwapInt64(value);
#elif defined(__sun) || defined(sun)
		return BSWAP_64(value);
#elif defined(__FreeBSD__)
		return bswap64(value);
#elif defined(__OpenBSD__)
		return swap64(value);
#elif defined(__NetBSD__)
		return bswap64(value);
#elif defined(_MSC_VER) //&& !defined(_DEBUG)
#if defined __clang__	// clang-cl
		return __builtin_bswap64(value);
#else
		return _byteswap_uint64(value);
#endif
#else
		return (STENOS_EXTENSION((((value) & 0xff00000000000000ull) >> 56) | (((value) & 0x00ff000000000000ull) >> 40) | (((value) & 0x0000ff0000000000ull) >> 24) |
					 (((value) & 0x000000ff00000000ull) >> 8) | (((value) & 0x00000000ff000000ull) << 8) | (((value) & 0x0000000000ff0000ull) << 24) |
					 (((value) & 0x000000000000ff00ull) << 40) | (((value) & 0x00000000000000ffull) << 56)));
#endif
	}

#if defined(__GNUC__) && !defined(__ICC)
#elif defined(__APPLE__)
#elif defined(__sun) || defined(sun)
#elif defined(__FreeBSD__)
#elif defined(__OpenBSD__)
#elif defined(__NetBSD__)
#elif defined(_MSC_VER)
#else
#define STENOS_NO_FAST_BSWAP
#endif

	/// @brief Write 16 bits integer value to dst in little endian order
	STENOS_ALWAYS_INLINE void write_LE_16(void* dst, std::uint16_t value)
	{
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		value = byte_swap_16(value);
#endif
		memcpy(dst, &value, sizeof(std::uint16_t));
	}
	/// @brief Write 32 bits integer value to dst in little endian order
	STENOS_ALWAYS_INLINE void write_LE_32(void* dst, std::uint32_t value)
	{
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		value = byte_swap_32(value);
#endif
		memcpy(dst, &value, sizeof(std::uint32_t));
	}
	/// @brief Write 64 bits integer value to dst in little endian order
	STENOS_ALWAYS_INLINE void write_LE_64(void* dst, std::uint64_t value)
	{
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		value = byte_swap_64(value);
#endif
		memcpy(dst, &value, sizeof(std::uint64_t));
	}

	/// @brief Write 64 bits integer value to dst in big endian order
	STENOS_ALWAYS_INLINE void write_BE_64(void* dst, std::uint64_t value)
	{
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_BIG_ENDIAN
		value = byte_swap_64(value);
#endif
		memcpy(dst, &value, sizeof(std::uint64_t));
	}

	/// @brief Write size_t object to dst
	STENOS_ALWAYS_INLINE void write_size_t(void* dst, size_t value)
	{
		memcpy(dst, &value, sizeof(size_t));
	}

	/// @brief Read 16 bits integer from src in little endian order
	STENOS_ALWAYS_INLINE auto read_LE_16(const void* src) -> std::uint16_t
	{
		std::uint16_t value = 0;
		memcpy(&value, src, sizeof(std::uint16_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		value = byte_swap_16(value);
#endif
		return value;
	}
	/// @brief Read 32 bits integer from src in little endian order
	STENOS_ALWAYS_INLINE auto read_LE_32(const void* src) -> std::uint32_t
	{
		std::uint32_t value = 0;
		memcpy(&value, src, sizeof(std::uint32_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		value = byte_swap_32(value);
#endif
		return value;
	}
	/// @brief Read 64 bits integer from src in little endian order
	STENOS_ALWAYS_INLINE auto read_LE_64(const void* src) -> std::uint64_t
	{
		std::uint64_t value = 0;
		memcpy(&value, src, sizeof(std::uint64_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		value = byte_swap_64(value);
#endif
		return value;
	}

	/// @brief Reads 16 bits integer from src
	STENOS_ALWAYS_INLINE auto read_16(const void* src) -> std::uint16_t
	{
		std::uint16_t value = 0;
		memcpy(&value, src, sizeof(std::uint16_t));
		return value;
	}
	/// @brief Reads 32 bits integer from src
	STENOS_ALWAYS_INLINE auto read_32(const void* src) -> std::uint32_t
	{
		std::uint32_t value = 0;
		memcpy(&value, src, sizeof(std::uint32_t));
		return value;
	}
	/// @brief Reads 64 bits integer from src
	STENOS_ALWAYS_INLINE auto read_64(const void* src) -> std::uint64_t
	{
		std::uint64_t value = 0;
		memcpy(&value, src, sizeof(std::uint64_t));
		return value;
	}

	/// @brief Reads uintptr_t integer from src
	STENOS_ALWAYS_INLINE auto read_ptr_t(const void* src) -> std::uintptr_t
	{
		std::uintptr_t value = 0;
		memcpy(&value, src, sizeof(std::uintptr_t));
		return value;
	}

	/// @brief Reads 16 bits integer from src in big endian order
	STENOS_ALWAYS_INLINE auto read_BE_16(const void* src) -> std::uint16_t
	{
		std::uint16_t value = 0;
		memcpy(&value, src, sizeof(std::uint16_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_BIG_ENDIAN
		value = byte_swap_16(value);
#endif
		return value;
	}
	/// @brief Reads 32 bits integer from src in big endian order
	STENOS_ALWAYS_INLINE auto read_BE_32(const void* src) -> std::uint32_t
	{
		std::uint32_t value = 0;
		memcpy(&value, src, sizeof(std::uint32_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_BIG_ENDIAN
		value = byte_swap_32(value);
#endif
		return value;
	}
	/// @brief Reads 64 bits integer from src in big endian order
	STENOS_ALWAYS_INLINE auto read_BE_64(const void* src) -> std::uint64_t
	{
		std::uint64_t value = 0;
		memcpy(&value, src, sizeof(std::uint64_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_BIG_ENDIAN
		value = byte_swap_64(value);
#endif
		return value;
	}

	/// @brief Reads size_t object from src
	STENOS_ALWAYS_INLINE auto read_size_t(const void* src) -> size_t
	{
		size_t res = 0;
		memcpy(&res, src, sizeof(size_t));
		return res;
	}
	/// @brief Reads size_t object from src in little endian order
	STENOS_ALWAYS_INLINE auto read_LE_size_t(const void* src) -> size_t
	{
		size_t res = 0;
		memcpy(&res, src, sizeof(size_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_LITTLE_ENDIAN
		if (sizeof(size_t) == 8)
			res = byte_swap_64(res);
		else
			res = byte_swap_32(res);
#endif
		return res;
	}
	/// @brief Reads size_t object from src in big endian order
	STENOS_ALWAYS_INLINE auto read_BE_size_t(const void* src) -> size_t
	{
		size_t res = 0;
		memcpy(&res, src, sizeof(size_t));
#if STENOS_BYTEORDER_ENDIAN != STENOS_BYTEORDER_BIG_ENDIAN
		if (sizeof(size_t) == 8) {
			res = static_cast<size_t>(byte_swap_64(res));
		}
		else {
			res = static_cast<size_t>(byte_swap_32(static_cast<std::uint32_t>(res)));
		}
#endif
		return res;
	}

	inline void print_features()
	{
		std::printf("Has builtin expect: ");
#ifdef STENOS_HAS_EXPECT
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has aligned malloc: ");
#ifdef STENOS_HAS_ALIGNED_ALLOCATION
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has always inline: ");
#ifdef STENOS_HAS_ALWAYS_INLINE
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has asm popcnt: ");
#ifdef STENOS_HAS_ASM_POPCNT
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has builtin popcnt: ");
#ifdef STENOS_HAS_BUILTIN_POPCNT
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has builtin bit scan forward/backward: ");
#ifdef STENOS_HAS_BUILTIN_BITSCAN
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has builtin byte swap: ");
#ifdef STENOS_HAS_BUILTIN_BYTESWAP
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif

		std::printf("Has BMI2: ");
#ifdef __BMI2__
		std::printf("yes\n");
#else
		std::printf("no\n");
#endif
	}

} // end namespace stenos

#ifdef __GNUC__
// #pragma GCC diagnostic pop
#endif

#undef max
#undef min

/** @}*/
// end bits

#endif
