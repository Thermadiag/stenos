/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

/*  Shuffle/unshuffle routines which dynamically dispatch to hardware-
    accelerated routines based on the processor's architecture.
    Consumers should almost always prefer to call these routines instead
    of directly calling one of the hardware-accelerated routines, since
    these are cross-platform and future-proof. */

#ifndef STENOS_SHUFFLE_H
#define STENOS_SHUFFLE_H

#include "../bits.hpp"
#include "simd.h"

namespace stenos
{
	/**
	  Primary shuffle and bitshuffle routines.
	  This function dynamically dispatches to the appropriate hardware-accelerated
	  routine based on the host processor's architecture. If the host processor is
	  not supported by any of the hardware-accelerated routines, the generic
	  (non-accelerated) implementation is used instead.
	  Consumers should almost always prefer to call this routine instead of directly
	  calling the hardware-accelerated routines because this method is both cross-
	  platform and future-proof.
	*/
	void shuffle(const size_t bytesoftype, const size_t blocksize, const uint8_t* _src, uint8_t* _dest) noexcept;

	/**
	  Primary unshuffle and bitunshuffle routine.
	  This function dynamically dispatches to the appropriate hardware-accelerated
	  routine based on the host processor's architecture. If the host processor is
	  not supported by any of the hardware-accelerated routines, the generic
	  (non-accelerated) implementation is used instead.
	  Consumers should almost always prefer to call this routine instead of directly
	  calling the hardware-accelerated routines because this method is both cross-
	  platform and future-proof.
	*/
	void unshuffle(const size_t bytesoftype, const size_t blocksize, const uint8_t* _src, uint8_t* _dest) noexcept;

}

#endif /* SHUFFLE_H */
