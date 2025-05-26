#ifndef FAST_VECTOR_MATH_H
#define FAST_VECTOR_MATH_H

#include <immintrin.h>

__m128 sse2_rint_ps(__m128 x);

#include "simd_sse2.def.h"
#include "speed_delta_job.inl"
void SIMD_POSTFIX(calc_speed_delta_walk_worker)(int thread_id, int total_threads, void *job_data);
void SIMD_POSTFIX(calc_speed_delta_air_worker)(int thread_id, int total_threads, void *job_data);

#include "simd_sse41.def.h"
#include "speed_delta_job.inl"
void SIMD_POSTFIX(calc_speed_delta_walk_worker)(int thread_id, int total_threads, void *job_data);
void SIMD_POSTFIX(calc_speed_delta_air_worker)(int thread_id, int total_threads, void *job_data);

#include "simd_avx.def.h"
#include "speed_delta_job.inl"
void SIMD_POSTFIX(calc_speed_delta_walk_worker)(int thread_id, int total_threads, void *job_data);
void SIMD_POSTFIX(calc_speed_delta_air_worker)(int thread_id, int total_threads, void *job_data);

#include "simd.undef.h"

#endif // FAST_VECTOR_MATH_H
