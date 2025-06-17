/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>
  Creation date: 2009-05-20

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#include "shuffle.h"
#include "shuffle-generic.h"
#include "simd.h"
#include <cstdio>

/*  Include hardware-accelerated shuffle/unshuffle routines based on
    the target architecture. Note that a target architecture may support
    more than one type of acceleration!*/
#if defined(__AVX2__)
#include "shuffle-avx2.h"
#endif /* defined(__AVX2__) */

#if defined(__SSE2__)
#include "shuffle-sse2.h"
#endif /* defined(__SSE2__) */

namespace stenos
{

	/*  Define function pointer types for shuffle/unshuffle routines. */
	typedef void (*shuffle_func)(int32_t, int32_t, const uint8_t*, uint8_t*);
	typedef void (*unshuffle_func)(int32_t, int32_t, const uint8_t*, uint8_t*);

	/* An implementation of shuffle/unshuffle routines. */
	struct shuffle_implementation_t
	{
		/* Name of this implementation. */
		const char* name;
		/* Function pointer to the shuffle routine for this implementation. */
		shuffle_func shuffle;
		/* Function pointer to the unshuffle routine for this implementation. */
		unshuffle_func unshuffle;
	};

	static inline shuffle_implementation_t get_shuffle_implementation(void)
	{
		shuffle_implementation_t impl_generic;

#if defined(__AVX2__)
		if (stenos::cpu_features().HAS_AVX2) {
			shuffle_implementation_t impl_avx2;
			impl_avx2.name = "avx2";
			impl_avx2.shuffle = (shuffle_func)shuffle_avx2;
			impl_avx2.unshuffle = (unshuffle_func)unshuffle_avx2;
			return impl_avx2;
		}
#endif /* defined(__AVX2__) */

#if defined(__SSE2__)
		if (stenos::cpu_features().HAS_SSE2) {
			shuffle_implementation_t impl_sse2;
			impl_sse2.name = "sse2";
			impl_sse2.shuffle = (shuffle_func)shuffle_sse2;
			impl_sse2.unshuffle = (unshuffle_func)unshuffle_sse2;
			return impl_sse2;
		}
#endif /* defined(__SSE2__) */

		/*  Processor doesn't support any of the hardware-accelerated implementations,
		    so use the generic implementation. */
		impl_generic.name = "generic";
		impl_generic.shuffle = (shuffle_func)shuffle_generic;
		impl_generic.unshuffle = (unshuffle_func)unshuffle_generic;
		return impl_generic;
	}

	/*  The dynamically-chosen shuffle/unshuffle implementation.
	    This is only safe to use once `implementation_initialized` is set. */
	static shuffle_implementation_t host_implementation = get_shuffle_implementation();

	/*  Shuffle a block by dynamically dispatching to the appropriate
	    hardware-accelerated routine at run-time. */
	void shuffle(size_t bytesoftype, size_t blocksize, const uint8_t* _src, uint8_t* _dest) noexcept
	{
		/*  The implementation is initialized.
		    Dispatch to it's shuffle routine. */
		if (bytesoftype == 1)
			memcpy((void*)_dest, _src, 256);
		else
			(host_implementation.shuffle)((int32_t)bytesoftype, (int32_t)blocksize, _src, _dest);
	}

	/*  Unshuffle a block by dynamically dispatching to the appropriate
	    hardware-accelerated routine at run-time. */
	void unshuffle(size_t bytesoftype, size_t blocksize, const uint8_t* _src, uint8_t* _dest) noexcept
	{
		/*  The implementation is initialized.
		    Dispatch to it's unshuffle routine. */
		if (bytesoftype == 1)
			memcpy((void*)_dest, _src, 256);
		else
			(host_implementation.unshuffle)((int32_t)bytesoftype, (int32_t)blocksize, _src, _dest);
	}

}
