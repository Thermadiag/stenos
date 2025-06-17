/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Copyright (c) 2021  Blosc Development Team <blosc@blosc.org>
  https://blosc.org
  License: BSD 3-Clause (see LICENSE.txt)

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

/*********************************************************************
  Generic (non-hardware-accelerated) shuffle/unshuffle routines.
  These are used when hardware-accelerated functions aren't available
  for a particular platform; they are also used by the hardware-
  accelerated functions to handle any remaining elements in a block
  which isn't a multiple of the hardware's vector size.
**********************************************************************/

#ifndef BLOSC_SHUFFLE_GENERIC_H
#define BLOSC_SHUFFLE_GENERIC_H


#include <cstdint>
#include <cstring>
#include "../bits.hpp"

/**
  Generic (non-hardware-accelerated) shuffle routine.
  This is the pure element-copying nested loop. It is used by the
  generic shuffle implementation and also by the vectorized shuffle
  implementations to process any remaining elements in a block which
  is not a multiple of (type_size * vector_size).
*/
static inline void shuffle_generic_inline(const int32_t type_size,
                                   const int32_t vectorizable_blocksize, const int32_t blocksize,
                                   const uint8_t *_src, uint8_t *_dest) {
  int32_t i, j;
  /* Calculate the number of elements in the block. */
  const int32_t neblock_quot = blocksize / type_size;
  const int32_t neblock_rem = blocksize % type_size;
  const int32_t vectorizable_elements = vectorizable_blocksize / type_size;
  const int32_t inner_size = neblock_quot - vectorizable_elements;

  if (inner_size < 256 || (inner_size & 7)) {

	  /* Non-optimized shuffle */
	  for (j = 0; j < type_size; j++) {
		  for (i = vectorizable_elements; i < (int32_t)neblock_quot; i++) {
			  _dest[j * neblock_quot + i] = _src[i * type_size + j];
		  }
	  }
  }
  else {
	  int32_t sizes[8] = {
		  0, type_size, type_size * 2, type_size * 3, type_size * 4, type_size * 5, type_size * 6, type_size * 7,
	  };
	  /* Unrolled shuffle */
	  for (j = 0; j < type_size; j++) {
		  for (i = vectorizable_elements; i < (int32_t)neblock_quot; i += 8) {
			  auto* d = _dest + j * neblock_quot + i;
			  auto* s = _src + j + i * type_size;
			  d[0] = s[0];
			  d[1] = s[sizes[1]];
			  d[2] = s[sizes[2]];
			  d[3] = s[sizes[3]];
			  d[4] = s[sizes[4]];
			  d[5] = s[sizes[5]];
			  d[6] = s[sizes[6]];
			  d[7] = s[sizes[7]];
		  }
	  }
  }

  /* Copy any leftover bytes in the block without shuffling them. */
  memcpy(_dest + (blocksize - neblock_rem), _src + (blocksize - neblock_rem), neblock_rem);
}

/**
  Generic (non-hardware-accelerated) unshuffle routine.
  This is the pure element-copying nested loop. It is used by the
  generic unshuffle implementation and also by the vectorized unshuffle
  implementations to process any remaining elements in a block which
  is not a multiple of (type_size * vector_size).
*/
static inline void unshuffle_generic_inline(const int32_t type_size,
                                     const int32_t vectorizable_blocksize, const int32_t blocksize,
                                     const uint8_t *_src, uint8_t *_dest) {
  int32_t i, j;

  /* Calculate the number of elements in the block. */
  const int32_t neblock_quot = blocksize / type_size;
  const int32_t neblock_rem = blocksize % type_size;
  const int32_t vectorizable_elements = vectorizable_blocksize / type_size;
  const int32_t inner_size = neblock_quot - vectorizable_elements;


  if (inner_size < 256 || (inner_size & 7)) {
	  /* Non-optimized unshuffle */
	  for (i = vectorizable_elements; i < (int32_t)neblock_quot; i++) {
		  for (j = 0; j < type_size; j++) {
			  _dest[i * type_size + j] = _src[j * neblock_quot + i];
		  }
	  }
}
 else
{

	  // Unrolled unshuffle 
	  int32_t sizes[8] = {
		  0, type_size, type_size * 2, type_size * 3, type_size * 4, type_size * 5, type_size * 6, type_size * 7,
	  };
	  for (j = 0; j < type_size; j++) {
		  for (i = vectorizable_elements; i < (int32_t)neblock_quot; i += 8) {
			  auto d = _dest + i * type_size + j;
			  const uint64_t val = stenos::read_LE_64(_src + j * neblock_quot + i);
			  d[0] = (uint8_t)val;
			  d[sizes[1]] = (uint8_t)(val >> 8u);
			  d[sizes[2]] = (uint8_t)(val >> 16u);
			  d[sizes[3]] = (uint8_t)(val >> 24u);
			  d[sizes[4]] = (uint8_t)(val >> 32u);
			  d[sizes[5]] = (uint8_t)(val >> 40u);
			  d[sizes[6]] = (uint8_t)(val >> 48u);
			  d[sizes[7]] = (uint8_t)(val >> 56u);
		  }
	  }
  }


  /* Copy any leftover bytes in the block without unshuffling them. */
  memcpy(_dest + (blocksize - neblock_rem), _src + (blocksize - neblock_rem), neblock_rem);
}

/**
  Generic (non-hardware-accelerated) shuffle routine.
*/
void shuffle_generic(const int32_t bytesoftype, const int32_t blocksize,
                                     const uint8_t *_src, uint8_t *_dest);

/**
  Generic (non-hardware-accelerated) unshuffle routine.
*/
void unshuffle_generic(const int32_t bytesoftype, const int32_t blocksize,
                                       const uint8_t *_src, uint8_t *_dest);

#endif /* BLOSC_SHUFFLE_GENERIC_H */
