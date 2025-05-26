#define SIMD_WIDTH 4
#define SIMD_TYPE __m128
#define SIMD(fn) _mm_##fn
#define SIMD_POSTFIX(fn) fn##_sse2
#define SIMD_ROUND_SSE2 1
