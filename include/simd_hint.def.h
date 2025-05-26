#ifndef SIMD_WIDTH
  #define SIMD_WIDTH 8
#endif
#ifndef SIMD_TYPE
  #define SIMD_TYPE __m256
#endif
#ifndef SIMD
  #define SIMD(fn) _mm256_##fn
#endif
#ifndef SIMD_POSTFIX
  #define SIMD_POSTFIX(fn) fn##_avx
#endif
#ifndef SIMD_ROUND_SSE2
  #define SIMD_ROUND_SSE2 0
#endif
