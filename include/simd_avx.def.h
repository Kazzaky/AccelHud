#define SIMD_WIDTH 8
#define SIMD_TYPE __m256
#define SIMD(fn) _mm256_##fn
#define SIMD_SUFFIX(fn) fn##_avx
#define SIMD_ROUND_SSE2 0
