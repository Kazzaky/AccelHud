#include <immintrin.h>
#include "q_shared.h"
#include "simd_hint.def.h"

// only in case we need orig there is _sf suffix,
// otherwise its automatically sf without suffix in name
// sf = snapped and after friction

typedef struct {
  // in's
  int       resolution;               // aka count
  float     *sin_table;               // [resolution]
  float     *cos_table;               // [resolution]

  // const
  SIMD_TYPE v_zero;
  SIMD_TYPE v_one;

  // preload to save ops
  SIMD_TYPE v_wishdir[3];
  SIMD_TYPE v_velocity_sf[3];
  SIMD_TYPE v_vel_up_dot;
  SIMD_TYPE v_vel_length;             // aka speed
  SIMD_TYPE v_wishspeed;
  SIMD_TYPE v_vel_wish_z_square_sf;   // snapped and after friction, never change

  SIMD_TYPE v_overbounce;
  SIMD_TYPE v_normal[3];

  float     accel;
  float     wishspeed;                // redundant but save overheat

  // air specific
  int       air_control;
  int       ground_plane;

  // air cpm specific
  int       sidemove;
  SIMD_TYPE v_velocity[3];
  SIMD_TYPE v_vel_wish_z_square;      // original (before snap and friction), never change
  

  // out's
  float     *speed_delta_total;       // [resolution]
  float     *speed_delta_forward;     // [resolution]
  float     *speed_delta_side;        // [resolution]
  float     *speed_delta_up;          // [resolution]
  float     *speed_delta_plane;       // [resolution]
} SIMD_SUFFIX(speed_delta_job_t);

void SIMD_SUFFIX(set_speed_delta_job_data)(
  void    *job_data,
  // in's
  int     resolution,           // aka count
  float   *sin_table,           // [resolution]
  float   *cos_table,           // [resolution]

  vec3_t  wishdir,
  float   wishspeed,
  float   accel,
  
  vec3_t  velocity_sf,         // snapped and after friction
  vec3_t  normal,

  // air specific
  int     air_control,
  int     ground_plane,

  // air cpm specific
  int     sidemove,
  vec3_t  velocity,            // original (before snap and friction)

  // out's
  float   *speed_delta_total,   // [resolution]
  float   *speed_delta_forward, // [resolution]
  float   *speed_delta_side,    // [resolution]
  float   *speed_delta_up,      // [resolution]
  float   *speed_delta_plane    // [resolution]
);
