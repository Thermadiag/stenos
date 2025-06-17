/*
   LZ4 - Fast LZ compression algorithm
   Copyright (C) 2011-2017, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
    - LZ4 homepage : http://www.lz4.org
    - LZ4 source repository : https://github.com/lz4/lz4
*/

#include "lz4dry.h"
#include <limits>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wunused-macros"

#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-macros"

#endif

namespace stenos
{

/*
 * ACCELERATION_DEFAULT :
 * Select "acceleration" for lz4_compress_fast() when parameter value <= 0
 */
#define ACCELERATION_DEFAULT 1

/*
 * LZ4_FORCE_SW_BITCOUNT
 * Define this parameter if your target system or compiler does not support hardware bit count
 */
#if defined(_MSC_VER) && defined(_WIN32_WCE) /* Visual Studio for Windows CE does not support Hardware bit count */
#define LZ4_FORCE_SW_BITCOUNT
#endif

/**
  Introduction

  LZ4 is lossless compression algorithm, providing compression speed at 400 MB/s per core,
  scalable with multi-cores CPU. It features an extremely fast decoder, with speed in
  multiple GB/s per core, typically reaching RAM speed limits on multi-core systems.

  The LZ4 compression library provides in-memory compression and decompression functions.
  Compression can be done in:
    - a single step (described as Simple Functions)
    - a single step, reusing a context (described in Advanced Functions)
    - unbounded multiple steps (described as Streaming compression)

  lz4.h provides block compression functions. It gives full buffer control to user.
  Decompressing an lz4-compressed block also requires metadata (such as compressed size).
  Each application is free to encode such metadata in whichever way it wants.

  An additional format, called LZ4 frame specification (doc/lz4_Frame_format.md),
  take care of encoding standard metadata alongside LZ4-compressed blocks.
  If your application requires interoperability, it's recommended to use it.
  A library is provided to take care of it, see lz4frame.h.
*/

/*------   Version   ------*/
#define LZ4_VERSION_MAJOR 1   /* for breaking interface changes  */
#define LZ4_VERSION_MINOR 8   /* for new (non-breaking) interface capabilities */
#define LZ4_VERSION_RELEASE 1 /* for tweaks, bug-fixes, or development */

#define LZ4_VERSION_NUMBER (LZ4_VERSION_MAJOR * 100 * 100 + LZ4_VERSION_MINOR * 100 + LZ4_VERSION_RELEASE)

#define LZ4_LIB_VERSION LZ4_VERSION_MAJOR.LZ4_VERSION_MINOR.LZ4_VERSION_RELEASE
#define LZ4_QUOTE(str) #str
#define LZ4_EXPAND_AND_QUOTE(str) LZ4_QUOTE(str)
#define LZ4_VERSION_STRING LZ4_EXPAND_AND_QUOTE(LZ4_LIB_VERSION)

/*-************************************
 *  Tuning parameter
 **************************************/
/*!
 * LZ4_MEMORY_USAGE :
 * Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
 * Increasing memory usage improves compression ratio
 * Reduced memory usage may improve speed, thanks to cache effect
 * Default value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
 */
#ifndef LZ4_MEMORY_USAGE
#define LZ4_MEMORY_USAGE 10
#endif

/*-************************************
 *  Advanced Functions
 **************************************/
#define LZ4_MAX_INPUT_SIZE 0x7E000000 /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize) ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize) / 255) + 16)

	/*-*********************************************
	 *  Streaming Compression Functions
	 ***********************************************/
	typedef union LZ4_stream_u LZ4_stream_t; /* incomplete type (defined later) */

	/*^**********************************************
	 * !!!!!!   STATIC LINKING ONLY   !!!!!!
	 ***********************************************/
	/*-************************************
	 *  Private definitions
	 **************************************
	 * Do not use these definitions.
	 * They are exposed to allow static allocation of `LZ4_stream_t` and `LZ4_streamDecode_t`.
	 * Using these definitions will expose code to API and/or ABI break in future versions of the library.
	 **************************************/
#define LZ4_HASHLOG (LZ4_MEMORY_USAGE - 2)
#define LZ4_HASHTABLESIZE (1 << LZ4_MEMORY_USAGE)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG) /* required as macro for static allocation */

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#include <stdint.h>

	typedef struct
	{
		uint32_t hashTable[LZ4_HASH_SIZE_U32];
		uint32_t currentOffset;
		uint32_t initCheck;
		const uint8_t* dictionary;
		uint8_t* bufferStart; /* obsolete, used for slideInputBuffer */
		uint32_t dictSize;
	} LZ4_stream_t_internal;

	typedef struct
	{
		const uint8_t* externalDict;
		size_t extDictSize;
		const uint8_t* prefixEnd;
		size_t prefixSize;
	} LZ4_streamDecode_t_internal;

#else

	typedef struct
	{
		unsigned int hashTable[LZ4_HASH_SIZE_U32];
		unsigned int currentOffset;
		unsigned int initCheck;
		const unsigned char* dictionary;
		unsigned char* bufferStart; /* obsolete, used for slideInputBuffer */
		unsigned int dictSize;
	} LZ4_stream_t_internal;

	typedef struct
	{
		const unsigned char* externalDict;
		size_t extDictSize;
		const unsigned char* prefixEnd;
		size_t prefixSize;
	} LZ4_streamDecode_t_internal;

#endif

/*!
 * LZ4_stream_t :
 * information structure to track an LZ4 stream.
 * init this structure before first use.
 * note : only use in association with static linking !
 *        this definition is not API/ABI safe,
 *        it may change in a future version !
 */
#define LZ4_STREAMSIZE_U64 ((1 << (LZ4_MEMORY_USAGE - 3)) + 4)
#define LZ4_STREAMSIZE (LZ4_STREAMSIZE_U64 * sizeof(unsigned long long))
	union LZ4_stream_u
	{
		unsigned long long table[LZ4_STREAMSIZE_U64];
		LZ4_stream_t_internal internal_donotuse;
	}; /* previously typedef'd to LZ4_stream_t */

/*!
 * LZ4_streamDecode_t :
 * information structure to track an LZ4 stream during decompression.
 * init this structure  using LZ4_setStreamDecode (or memset()) before first use
 * note : only use in association with static linking !
 *        this definition is not API/ABI safe,
 *        and may change in a future version !
 */
#define LZ4_STREAMDECODESIZE_U64 4
#define LZ4_STREAMDECODESIZE (LZ4_STREAMDECODESIZE_U64 * sizeof(unsigned long long))
	union LZ4_streamDecode_u
	{
		unsigned long long table[LZ4_STREAMDECODESIZE_U64];
		LZ4_streamDecode_t_internal internal_donotuse;
	}; /* previously typedef'd to LZ4_streamDecode_t */

/*-************************************
 *  Obsolete Functions
 **************************************/

/*! Deprecation warnings
   Should deprecation warnings be a problem,
   it is generally possible to disable them,
   typically with -Wno-deprecated-declarations for gcc
   or _CRT_SECURE_NO_WARNINGS in Visual.
   Otherwise, it's also possible to define LZ4_DISABLE_DEPRECATE_WARNINGS */
#ifdef LZ4_DISABLE_DEPRECATE_WARNINGS
#define LZ4_DEPRECATED(message) /* disable deprecation warnings */
#else
#define LZ4_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#if defined(__cplusplus) && (__cplusplus >= 201402) /* C++14 or greater */
#define LZ4_DEPRECATED(message) [[deprecated(message)]]
#elif (LZ4_GCC_VERSION >= 405) || defined(__clang__)
#define LZ4_DEPRECATED(message) __attribute__((deprecated(message)))
#elif (LZ4_GCC_VERSION >= 301)
#define LZ4_DEPRECATED(message) __attribute__((deprecated))
#elif defined(_MSC_VER)
#define LZ4_DEPRECATED(message) __declspec(deprecated(message))
#else
#pragma message("WARNING: You need to implement LZ4_DEPRECATED for this compiler")
#define LZ4_DEPRECATED(message)
#endif
#endif /* LZ4_DISABLE_DEPRECATE_WARNINGS */

/* see also "memory routines" below */
#include "../bits.hpp"

/*-************************************
 *  Compiler Options
 **************************************/
#ifdef _MSC_VER /* Visual Studio */
#include <intrin.h>
#pragma warning(disable : 4127) /* disable: C4127: conditional expression is constant */
#pragma warning(disable : 4293) /* disable: C4293: too large shift (32-bits) */
#endif				/* _MSC_VER */

#ifndef LZ4_FORCE_INLINE
#ifdef _MSC_VER /* Visual Studio */
#define LZ4_FORCE_INLINE static __forceinline
#else
#if defined(__cplusplus) || defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L /* C99 */
#ifdef __GNUC__
#define LZ4_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define LZ4_FORCE_INLINE static inline
#endif
#else
#define LZ4_FORCE_INLINE static
#endif /* __STDC_VERSION__ */
#endif /* _MSC_VER */
#endif /* LZ4_FORCE_INLINE */

/* LZ4_FORCE_O2_GCC_PPC64LE and LZ4_FORCE_O2_INLINE_GCC_PPC64LE
 * Gcc on ppc64le generates an unrolled SIMDized loop for LZ4_wildCopy,
 * together with a simple 8-byte copy loop as a fall-back path.
 * However, this optimization hurts the decompression speed by >30%,
 * because the execution does not go to the optimized loop
 * for typical compressible data, and all of the preamble checks
 * before going to the fall-back path become useless overhead.
 * This optimization happens only with the -O3 flag, and -O2 generates
 * a simple 8-byte copy loop.
 * With gcc on ppc64le, all of the LZ4_decompress_* and LZ4_wildCopy
 * functions are annotated with __attribute__((optimize("O2"))),
 * and also LZ4_wildCopy is forcibly inlined, so that the O2 attribute
 * of LZ4_wildCopy does not affect the compression speed.
 */
#if defined(__PPC64__) && defined(__LITTLE_ENDIAN__) && defined(__GNUC__)
#define LZ4_FORCE_O2_GCC_PPC64LE __attribute__((optimize("O2")))
#define LZ4_FORCE_O2_INLINE_GCC_PPC64LE __attribute__((optimize("O2"))) LZ4_FORCE_INLINE
#else
#define LZ4_FORCE_O2_GCC_PPC64LE
#define LZ4_FORCE_O2_INLINE_GCC_PPC64LE static
#endif

#if (defined(__GNUC__) && (__GNUC__ >= 3)) || (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 800)) || defined(__clang__)
#define expect(expr, value) (__builtin_expect((expr), (value)))
#else
#define expect(expr, value) (expr)
#endif

/*-************************************
 *  Memory routines
 **************************************/
#include <stdlib.h> /* malloc, calloc, free */
#define ALLOCATOR(n, s) calloc(n, s)
#define FREEMEM free
#include <string.h> /* memset, memcpy */
#define MEM_INIT memset

/*-************************************
 *  Basic Types
 **************************************/
#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#include <stdint.h>
	typedef uint8_t BYTE;
	typedef uint16_t U16;
	typedef uint32_t U32;
	typedef int32_t S32;
	typedef uint64_t U64;
	typedef uintptr_t uptrval;
#else
	typedef unsigned char BYTE;
	typedef unsigned short U16;
	typedef unsigned int U32;
	typedef signed int S32;
	typedef unsigned long long U64;
	typedef size_t uptrval; /* generally true, except OpenVMS-64 */
#endif

#if defined(__x86_64__)
	typedef U64 reg_t; /* 64-bits in x32 mode */
#else
	typedef size_t reg_t; /* 32-bits in x32 mode */
#endif

	/*-************************************
	 *  Reading and writing into memory
	 **************************************/
	LZ4_FORCE_INLINE unsigned LZ4_isLittleEndian(void) noexcept
	{
		const union
		{
			U32 u;
			BYTE c[4];
		} one = { 1 }; /* don't use static : performance detrimental */
		return one.c[0];
	}

	LZ4_FORCE_INLINE U16 LZ4_read16(const void* memPtr) noexcept
	{
		U16 val;
		memcpy(&val, memPtr, sizeof(val));
		return val;
	}

	LZ4_FORCE_INLINE U32 LZ4_read32(const void* memPtr) noexcept
	{
		U32 val;
		memcpy(&val, memPtr, sizeof(val));
		return val;
	}

	LZ4_FORCE_INLINE reg_t LZ4_read_ARCH(const void* memPtr) noexcept
	{
		reg_t val;
		memcpy(&val, memPtr, sizeof(val));
		return val;
	}

/*-************************************
 *  Common Constants
 **************************************/
#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH + MINMATCH)
	static const int LZ4_minLength = (MFLIMIT + 1);

#define KB *(1 << 10)
#define MB *(1 << 20)
#define GB *(1U << 30)

#define MAXD_LOG 16
#define MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

/*-************************************
 *  Error detection
 **************************************/
#if defined(LZ4_DEBUG) && (LZ4_DEBUG >= 1)
#include <assert.h>
#else
#ifndef assert
#define assert(condition) ((void)0)
#endif
#endif

#define LZ4_STATIC_ASSERT(c)                                                                                                                                                                           \
	{                                                                                                                                                                                              \
		enum                                                                                                                                                                                   \
		{                                                                                                                                                                                      \
			LZ4_static_assert = 1 / (int)(!!(c))                                                                                                                                           \
		};                                                                                                                                                                                     \
	} /* use only *after* variable declarations */

#if defined(LZ4_DEBUG) && (LZ4_DEBUG >= 2)
#include <stdio.h>
	static int g_debuglog_enable = 1;
#define DEBUGLOG(l, ...)                                                                                                                                                                               \
	{                                                                                                                                                                                              \
		if ((g_debuglog_enable) && (l <= LZ4_DEBUG)) {                                                                                                                                         \
			fprintf(stderr, __FILE__ ": ");                                                                                                                                                \
			fprintf(stderr, __VA_ARGS__);                                                                                                                                                  \
			fprintf(stderr, " \n");                                                                                                                                                        \
		}                                                                                                                                                                                      \
	}
#else
#define DEBUGLOG(l, ...)                                                                                                                                                                               \
	{                                                                                                                                                                                              \
	} /* disabled */
#endif

	/*-************************************
	 *  Common functions
	 **************************************/
	LZ4_FORCE_INLINE unsigned LZ4_NbCommonBytes(reg_t val) noexcept
	{
		if (LZ4_isLittleEndian()) {
			if (sizeof(val) == 8) {
#if defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
				unsigned long r = 0;
				_BitScanForward64(&r, (U64)val);
				return (unsigned)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
				return (__builtin_ctzll((U64)val) >> 3);
#else
				static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2, 0, 3, 1, 3, 1, 4, 2, 7, 0, 2, 3, 6, 1, 5, 3, 5, 1, 3, 4, 4, 2, 5, 6, 7,
									 7, 0, 1, 2, 3, 3, 4, 6, 2, 6, 5, 5, 3, 4, 5, 6, 7, 1, 2, 4, 6, 4, 4, 5, 7, 2, 6, 5, 7, 6, 7, 7 };
				return DeBruijnBytePos[((U64)((val & -(long long)val) * 0x0218A392CDABBD3FULL)) >> 58];
#endif
			}
			else /* 32 bits */ {
#if defined(_MSC_VER) && !defined(LZ4_FORCE_SW_BITCOUNT)
				unsigned long r;
				_BitScanForward(&r, static_cast<U32>(val));
				return (unsigned)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
				return (__builtin_ctz((U32)(val)) >> 3);
#else
				static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0, 3, 2, 2, 1, 3, 2, 0, 1, 3, 3, 1, 2, 2, 2, 2, 0, 3, 1, 2, 0, 1, 0, 1, 1 };
				return DeBruijnBytePos[((U32)((val & -(S32)(val)) * 0x077CB531U)) >> 27];
#endif
			}
		}
		else /* Big Endian CPU */ {
			if (sizeof(val) == 8) { /* 64-bits */
#if defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
				unsigned long r = 0;
				_BitScanReverse64(&r, val);
				return (unsigned)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
				return (__builtin_clzll((U64)(val)) >> 3);
#else
				static const U32 by32 = sizeof(val) * 4; /* 32 on 64 bits (goal), 16 on 32 bits.
				     Just to avoid some static analyzer complaining about shift by 32 on 32-bits target.
				     Note that this code path is never triggered in 32-bits mode. */
				unsigned r;
				if (!(val >> by32)) {
					r = 4;
				}
				else {
					r = 0;
					val >>= by32;
				}
				if (!(val >> 16)) {
					r += 2;
					val >>= 8;
				}
				else {
					val >>= 24;
				}
				r += (!val);
				return r;
#endif
			}
			else /* 32 bits */ {
#if defined(_MSC_VER) && !defined(LZ4_FORCE_SW_BITCOUNT)
				unsigned long r = 0;
				_BitScanReverse(&r, (unsigned long)val);
				return (unsigned)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && !defined(LZ4_FORCE_SW_BITCOUNT)
				return (__builtin_clz((U32)(val)) >> 3);
#else
				unsigned r;
				if (!(val >> 16)) {
					r = 2;
					val >>= 8;
				}
				else {
					r = 0;
					val >>= 24;
				}
				r += (!val);
				return r;
#endif
			}
		}
	}

#define STEPSIZE sizeof(reg_t)
	LZ4_FORCE_INLINE
	unsigned LZ4_count(const BYTE* pIn, const BYTE* pMatch, const BYTE* pInLimit) noexcept
	{
		const BYTE* const pStart = pIn;

		if STENOS_LIKELY (pIn < pInLimit - (STEPSIZE - 1)) {
			reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
			if (!diff) {
				pIn += STEPSIZE;
				pMatch += STEPSIZE;
			}
			else {
				return LZ4_NbCommonBytes(diff);
			}
		}

		while
			STENOS_LIKELY(pIn < pInLimit - (STEPSIZE - 1))
			{
				reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
				if (!diff) {
					pIn += STEPSIZE;
					pMatch += STEPSIZE;
					continue;
				}
				pIn += LZ4_NbCommonBytes(diff);
				return (unsigned)(pIn - pStart);
			}

		if ((STEPSIZE == 8) && (pIn < (pInLimit - 3)) && (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
			pIn += 4;
			pMatch += 4;
		}
		if ((pIn < (pInLimit - 1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
			pIn += 2;
			pMatch += 2;
		}
		if ((pIn < pInLimit) && (*pMatch == *pIn))
			pIn++;
		return (unsigned)(pIn - pStart);
	}

	/*-************************************
	 *  Local Constants
	 **************************************/
	static const int LZ4_64Klimit = (MAX_DISTANCE + (MFLIMIT - 1)); //((64 KB) + (MFLIMIT-1));
	static const U32 LZ4_skipTrigger = 6;				/* Increase this value ==> compression run slower on incompressible data */

	/*-************************************
	 *  Local Structures and types
	 **************************************/
	typedef enum
	{
		notLimited = 0,
		limitedOutput = 1
	} limitedOutput_directive;
	typedef enum
	{
		byU32,
		byU16
	} tableType_t;

	typedef enum
	{
		noDict = 0,
		withPrefix64k,
		usingExtDict
	} dict_directive;
	typedef enum
	{
		noDictIssue = 0,
		dictSmall
	} dictIssue_directive;

	typedef enum
	{
		endOnOutputSize = 0,
		endOnInputSize = 1
	} endCondition_directive;
	typedef enum
	{
		full = 0,
		partial = 1
	} earlyEnd_directive;

	/*-******************************
	 *  Compression functions
	 ********************************/
	LZ4_FORCE_INLINE U32 LZ4_hash4(U32 sequence, tableType_t const tableType) noexcept
	{
		if (tableType == byU16)
			return ((sequence * 2654435761U) >> ((MINMATCH * 8) - (LZ4_HASHLOG + 1)));
		else
			return ((sequence * 2654435761U) >> ((MINMATCH * 8) - LZ4_HASHLOG));
	}
	LZ4_FORCE_INLINE U32 LZ4_hashPosition(const void* const p, tableType_t const tableType) noexcept
	{
		return LZ4_hash4(LZ4_read32(p), tableType);
	}
	LZ4_FORCE_INLINE void LZ4_putPositionOnHash(const BYTE* p, U32 h, void* tableBase, tableType_t const tableType, const BYTE* srcBase) noexcept
	{
		switch (tableType) {
			case byU32: {
				U32* hashTable = (U32*)tableBase;
				hashTable[h] = (U32)(p - srcBase);
				return;
			}
			case byU16: {
				U16* hashTable = (U16*)tableBase;
				hashTable[h] = (U16)(p - srcBase);
				return;
			}
		}
	}

	LZ4_FORCE_INLINE void LZ4_putPosition(const BYTE* p, void* tableBase, tableType_t tableType, const BYTE* srcBase) noexcept
	{
		U32 const h = LZ4_hashPosition(p, tableType);
		LZ4_putPositionOnHash(p, h, tableBase, tableType, srcBase);
	}

	LZ4_FORCE_INLINE const BYTE* LZ4_getPositionOnHash(U32 h, void* tableBase, tableType_t tableType, const BYTE* srcBase) noexcept
	{
		if (tableType == byU32) {
			const U32* const hashTable = (U32*)tableBase;
			return hashTable[h] + srcBase;
		}
		{
			const U16* const hashTable = (U16*)tableBase;
			return hashTable[h] + srcBase;
		} /* default, to ensure a return */
	}

	LZ4_FORCE_INLINE const BYTE* LZ4_getPosition(const BYTE* p, void* tableBase, tableType_t tableType, const BYTE* srcBase) noexcept
	{
		U32 const h = LZ4_hashPosition(p, tableType);
		return LZ4_getPositionOnHash(h, tableBase, tableType, srcBase);
	}

	LZ4_FORCE_INLINE int LZ4_compress_generic_dry(LZ4_stream_t_internal* const cctx,
						      const char* const source,
						      const int inputSize,
						      const int maxOutputSize,
						      const limitedOutput_directive outputLimited,
						      const tableType_t tableType,
						      const U32 acceleration) noexcept
	{
		const BYTE* ip = (const BYTE*)source;
		const BYTE* base;
		const BYTE* lowLimit;
		const BYTE* anchor = (const BYTE*)source;
		const BYTE* const iend = ip + inputSize;
		const BYTE* const mflimit = iend - MFLIMIT;
		const BYTE* const matchlimit = iend - LASTLITERALS;

		int count = 0;

		U32 forwardH;

		/* Init conditions */
		if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE)
			return 0; /* Unsupported inputSize, too large (or negative) */

		base = (const BYTE*)source;
		lowLimit = (const BYTE*)source;

		if ((tableType == byU16) && (inputSize >= LZ4_64Klimit))
			return 0; /* Size too large (not within 64K limit) */
		if (inputSize < LZ4_minLength)
			goto _last_literals; /* Input too small, no compression (all literals) */

		/* First Byte */
		LZ4_putPosition(ip, cctx->hashTable, tableType, base);
		ip++;
		forwardH = LZ4_hashPosition(ip, tableType);

		/* Main Loop */
		for (;;) {
			ptrdiff_t refDelta = 0;
			const BYTE* match;

			/* Find a match */
			{
				const BYTE* forwardIp = ip;
				unsigned step = 1;
				unsigned searchMatchNb = acceleration << LZ4_skipTrigger;
				do {
					U32 const h = forwardH;
					ip = forwardIp;
					forwardIp +=  step;
					step = (searchMatchNb++ >> LZ4_skipTrigger);

					if STENOS_UNLIKELY (forwardIp > mflimit)
						goto _last_literals;

					match = LZ4_getPositionOnHash(h, cctx->hashTable, tableType, base);
					forwardH = LZ4_hashPosition(forwardIp, tableType);
					LZ4_putPositionOnHash(ip, h, cctx->hashTable, tableType, base);

				} while (((tableType == byU16) ? 0 : (match + MAX_DISTANCE < ip)) || (LZ4_read32(match + refDelta) != LZ4_read32(ip)));
			}

			/* Catch up */
			while (((ip > anchor) & (match + refDelta > lowLimit)) && ((ip[-1] == match[refDelta - 1]))) {
				ip--;
				match--;
			}

			/* Encode Literals */
			{
				int litLength = (int)(ip - anchor);
				count++;
				if ((outputLimited) && /* Check output buffer overflow */
				    ((count + litLength + (2 + 1 + LASTLITERALS) + (litLength / 255) > maxOutputSize)))
					return 0;
				if (litLength >= RUN_MASK) {
					int len = (int)litLength - RUN_MASK;
					/*for (; len >= 255; len -= 255)
						count++;
					count++;*/
					// TEST
					count += 1 + len / 256;
				}
				/* Copy Literals */
				count += litLength;
			}

		_next_match:
			/* Encode Offset */
			// LZ4_writeLE16(op, (U16)(ip-match)); op+=2;

			count += 2;

			/* Encode MatchLength */
			{
				int matchCode = (int)LZ4_count(ip + MINMATCH, match + MINMATCH, matchlimit);
				ip += MINMATCH + matchCode;

				if (outputLimited && /* Check output buffer overflow */
				    ((count + (1 + LASTLITERALS) + (matchCode >> 8) > maxOutputSize)))
					return 0;
				if (matchCode >= ML_MASK) {
					matchCode -= ML_MASK;

					while (matchCode >= 4 * 255) {
						count += 4;
						matchCode -= 4 * 255;
					}
					count += 1 + matchCode / 255;
				}
			}

			anchor = ip;

			/* Test end of chunk */
			if (ip > mflimit)
				break;

			/* Fill table */
			LZ4_putPosition(ip - 2, cctx->hashTable, tableType, base);

			/* Test next position */
			match = LZ4_getPosition(ip, cctx->hashTable, tableType, base);
			LZ4_putPosition(ip, cctx->hashTable, tableType, base);
			if ((match + MAX_DISTANCE >= ip) && (LZ4_read32(match + refDelta) == LZ4_read32(ip))) {
				++count;
				goto _next_match;
			}

			/* Prepare next loop */
			forwardH = LZ4_hashPosition(++ip, tableType);
		}

	_last_literals:
		/* Encode Last Literals */
		{
			int lastRun = (int)(iend - anchor);
			if ((outputLimited) && /* Check output buffer overflow */
			    ((count) + lastRun + 1 + ((lastRun + 255 - RUN_MASK) / 255) > (U32)maxOutputSize))
				return 0;
			if (lastRun >= RUN_MASK) {
				int accumulator = lastRun - RUN_MASK;
				/*++count;
				for (; accumulator >= 255; accumulator -= 255)
					++count;
				++count;*/
				// TEST
				count += 2 + (accumulator / 256);
			}
			else {
				++count;
			}
			count += lastRun;
		}

		return count;
	}

	LZ4_FORCE_INLINE void LZ4_resetStream(LZ4_stream_t* LZ4_stream) noexcept
	{
		DEBUGLOG(4, "LZ4_resetStream")
		MEM_INIT(LZ4_stream, 0, sizeof(LZ4_stream_t));
	}

	LZ4_FORCE_INLINE LZ4_stream_t* get_thread_safe_state() noexcept
	{
		static thread_local LZ4_stream_t state;
		return &state;
	}

	LZ4_FORCE_INLINE int LZ4_compress_fast_extState_dry(void* state, const char* source, int inputSize, int maxOutputSize, int acceleration) noexcept
	{
		state = state ? state : (void*)get_thread_safe_state();
		LZ4_stream_t_internal* ctx = &((LZ4_stream_t*)state)->internal_donotuse;
		LZ4_resetStream((LZ4_stream_t*)state);
		if (acceleration < 1)
			acceleration = ACCELERATION_DEFAULT;

		return LZ4_compress_generic_dry(ctx, source, inputSize, maxOutputSize, notLimited, byU32, acceleration);
	}

	size_t lz4_guess_size(const char* source, size_t inputSize, int acceleration) noexcept
	{
		return (size_t)LZ4_compress_fast_extState_dry(nullptr, source, (int)inputSize, 0, acceleration);
	}

	double lz4_guess_ratio(const char* source, size_t inputSize, int acceleration) noexcept
	{
		return (double)inputSize / (double)LZ4_compress_fast_extState_dry(nullptr, source, (int)inputSize, 0, acceleration);
	}

} // end namespace seq

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
