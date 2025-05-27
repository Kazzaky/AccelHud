#include "fast_vector_math.h"

__m128 sse2_rint_ps(__m128 x)
{
  __m128 two_to_23 = _mm_set1_ps(8388608.f); // too big to keep fractional part
  __m128 sign = _mm_and_ps(x, _mm_set1_ps(-.0f));
  __m128 abs = _mm_andnot_ps(_mm_set1_ps(-.0f), x);

  __m128 rounded = _mm_sub_ps(
    _mm_add_ps(abs, two_to_23),
    two_to_23
  );

  __m128 mask = _mm_cmplt_ps(abs, two_to_23);
  rounded = _mm_or_ps(_mm_and_ps(mask, rounded), _mm_andnot_ps(mask, abs));

  return _mm_or_ps(rounded, sign);
}

#include "simd_sse2.def.h"
#include "speed_delta_calc_impl.inl"

#include "simd_sse41.def.h"
#include "speed_delta_calc_impl.inl"

#include "simd_avx.def.h"
#include "speed_delta_calc_impl.inl"

#include "simd.undef.h"

