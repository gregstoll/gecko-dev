/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#ifndef AOM_AOM_DSP_X86_SYNONYMS_AVX2_H_
#define AOM_AOM_DSP_X86_SYNONYMS_AVX2_H_

#include <immintrin.h>

#include "config/aom_config.h"

#include "aom/aom_integer.h"

/**
 * Various reusable shorthands for x86 SIMD intrinsics.
 *
 * Intrinsics prefixed with xx_ operate on or return 128bit XMM registers.
 * Intrinsics prefixed with yy_ operate on or return 256bit YMM registers.
 */

// Loads and stores to do away with the tedium of casting the address
// to the right type.
static INLINE __m256i yy_load_256(const void *a) {
  return _mm256_load_si256((const __m256i *)a);
}

static INLINE __m256i yy_loadu_256(const void *a) {
  return _mm256_loadu_si256((const __m256i *)a);
}

static INLINE void yy_store_256(void *const a, const __m256i v) {
  _mm256_store_si256((__m256i *)a, v);
}

static INLINE void yy_storeu_256(void *const a, const __m256i v) {
  _mm256_storeu_si256((__m256i *)a, v);
}

// Fill an AVX register using an interleaved pair of values, ie. set the
// 16 channels to {a, b} repeated 8 times, using the same channel ordering
// as when a register is stored to / loaded from memory.
//
// This is useful for rearranging filter kernels for use with the _mm_madd_epi16
// instruction
static INLINE __m256i yy_set2_epi16(int16_t a, int16_t b) {
  return _mm256_setr_epi16(a, b, a, b, a, b, a, b, a, b, a, b, a, b, a, b);
}

// The _mm256_set1_epi64x() intrinsic is undefined for some Visual Studio
// compilers. The following function is equivalent to _mm256_set1_epi64x()
// acting on a 32-bit integer.
static INLINE __m256i yy_set1_64_from_32i(int32_t a) {
#if defined(_MSC_VER) && defined(_M_IX86) && _MSC_VER < 1900
  return _mm256_set_epi32(0, a, 0, a, 0, a, 0, a);
#else
  return _mm256_set1_epi64x((uint32_t)a);
#endif
}

// Some compilers don't have _mm256_set_m128i defined in immintrin.h. We
// therefore define an equivalent function using a different intrinsic.
// ([ hi ], [ lo ]) -> [ hi ][ lo ]
static INLINE __m256i yy_set_m128i(__m128i hi, __m128i lo) {
  return _mm256_insertf128_si256(_mm256_castsi128_si256(lo), hi, 1);
}

#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)

// _mm256_loadu2_m128i has been introduced in GCC 10.1
#if !defined(__clang__) && GCC_VERSION < 101000
static INLINE __m256i yy_loadu2_128(const void *hi, const void *lo) {
  __m128i mhi = _mm_loadu_si128((const __m128i *)(hi));
  __m128i mlo = _mm_loadu_si128((const __m128i *)(lo));
  return _mm256_set_m128i(mhi, mlo);
}
#else
static INLINE __m256i yy_loadu2_128(const void *hi, const void *lo) {
  __m128i mhi = _mm_loadu_si128((const __m128i *)(hi));
  __m128i mlo = _mm_loadu_si128((const __m128i *)(lo));
  return yy_set_m128i(mhi, mlo);
}
#endif

#undef GCC_VERSION

static INLINE void yy_storeu2_128(void *hi, void *lo, const __m256i a) {
  _mm_storeu_si128((__m128i *)hi, _mm256_extracti128_si256(a, 1));
  _mm_storeu_si128((__m128i *)lo, _mm256_castsi256_si128(a));
}

static INLINE __m256i yy_roundn_epu16(__m256i v_val_w, int bits) {
  const __m256i v_s_w = _mm256_srli_epi16(v_val_w, bits - 1);
  return _mm256_avg_epu16(v_s_w, _mm256_setzero_si256());
}
#endif  // AOM_AOM_DSP_X86_SYNONYMS_AVX2_H_
