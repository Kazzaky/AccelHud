#include <immintrin.h>
#include "simd_hint.def.h"

typedef struct {
  // in's
  const int       resolution;   // aka count
  const float     *sin_table;   // [resolution]
  const float     *cos_table;   // [resolution]

  // preload to save ops
  const SIMD_TYPE *v_wishdir;   // [3]
  const SIMD_TYPE *v_velocity;  // [3] (snapped and after friction !)
  const SIMD_TYPE *v_vel_up_dot;
  const SIMD_TYPE *v_vel_length;
  const SIMD_TYPE *v_wishspeed;
  const SIMD_TYPE *v_vel_wish_z_dot; // should be squared not dot // never change
  const SIMD_TYPE *v_overbounce;
  const SIMD_TYPE *v_normal;    // [3]

  const float     accel;
  const float     wishspeed; // redundant but save overheat

  // air specific
  const int       air_control;
  const int       ground_plane;

  // out's
  float           *speed_delta_total;   // [resolution]
  float           *speed_delta_forward; // [resolution]
  float           *speed_delta_side;    // [resolution]
  float           *speed_delta_up;      // [resolution]
} SIMD_POSTFIX(speed_delta_job_t);
