#include "fast_vector_math.h"
#include "bg_local.h"

#include "q_shared.h"
#include "simd_hint.def.h"

// **** following functions are SIMD version derived from following code: ****
// PM_Accelerate(...){
//   ...
//   // special case wishdir related values need to be adjusted (accel is different)
//   if (move_type == MOVE_AIR_CPM && (!a.pm.cmd.rightmove || a.pm.cmd.forwardmove))
//   {
//     if(DotProduct(a.pm_ps.velocity, wishdir_rotated) < 0){
//       speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, 2.5f, 0);
//     }else{
//       // this case is also required because the current move could have been ^ that one, which is no longer valid
//       speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, pm_airaccelerate, 0);
//     }
//   }
//   else
//   {
//     speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, accel_, 0);
//   }
//   ...
// }
//
// // the function to calculate delta
// // does not modify a.pm_ps.velocity (not sure anymore since its edited to be used in old version)
// static float calc_accelspeed(const vec3_t wishdir, float const wishspeed, float const accel_){
//   int    i;
//   float  addspeed, accelspeed, currentspeed;
//   vec3_t velpredict;
//   vec3_t velocity;
  
//   VectorCopy(a.pm_ps.velocity, velocity);

//   Sys_SnapVector(velocity); // solves bug in spectator mode

//   PM_Friction(velocity);

//   currentspeed = DotProduct (velocity, wishdir); // the velocity speed part regardless the wish direction

//   addspeed = wishspeed - currentspeed;

//   if (addspeed <= 0) {
//     return 0;
//   }

//   accelspeed = accel_*pm_frametime*wishspeed; // fixed pmove
//   if (accelspeed > addspeed) {
//       accelspeed = addspeed;
//   }

//   VectorCopy(velocity, velpredict);
    
//   for (i=0 ; i<3 ; i++) {
//     velpredict[i] += accelspeed*wishdir[i];
//   }

//   // add aircontrol to predict velocity vector
//   if(move_type == MOVE_AIR_CPM && wishspeed && !a.pm.cmd.forwardmove && a.pm.cmd.rightmove) PM_Aircontrol(wishdir, velpredict);

//   // clipping
//   if((move_type == MOVE_WALK || move_type == MOVE_WALK_SLICK))
//   {
//     float speed = VectorLength(velpredict);

//     PM_ClipVelocity(velpredict, a.pml.groundTrace.plane.normal,
//         velpredict, OVERCLIP );
        
//     VectorNormalize(velpredict);
//     VectorScale(velpredict, speed, velpredict);
//   }
//   else if((move_type == MOVE_AIR || move_type == MOVE_AIR_CPM)
//       && a.pml.groundPlane)
//   {
//     PM_ClipVelocity(velpredict, a.pml.groundTrace.plane.normal, velpredict, OVERCLIP );
//   }

//   // add snapping to predict velocity vector
//   Sys_SnapVector(velpredict);
  
//   return VectorLength(velpredict) - VectorLength(velocity);
// }

// even tho there are minor differences, just use single set function
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
){
  int i;
  SIMD_SUFFIX(speed_delta_job_t) *data = (SIMD_SUFFIX(speed_delta_job_t)*)(job_data);

  data->resolution = resolution;
  data->sin_table = sin_table;
  data->cos_table = cos_table;

  // const
  data->v_zero = SIMD(setzero_ps)();
  data->v_one = SIMD(set1_ps)(1.f);

  // preset / precalc
  for(i = 0; i < 3; ++i){
    data->v_wishdir[i] = SIMD(set1_ps)(wishdir[i]);
    data->v_velocity_sf[i] = SIMD(set1_ps)(velocity_sf[i]);
    data->v_normal[i] = SIMD(set1_ps)(normal[i]);
  }

  vec3_t vec_up = {0,0, 1.f};
  data->v_vel_up_dot = SIMD(set1_ps)(DotProduct(velocity_sf, vec_up));

  data->v_vel_length = SIMD(set1_ps)(VectorLength(velocity_sf));
  data->v_wishspeed = SIMD(set1_ps)(wishspeed);
  data->v_vel_wish_z_square_sf = SIMD(set1_ps)(velocity_sf[2] * wishdir[2]);

  data->v_overbounce = SIMD(set1_ps)(OVERCLIP);

  data->accel = accel;
  data->wishspeed = wishspeed;

  // air specific
  data->air_control = air_control;
  data->ground_plane = ground_plane;

  // air cpm specific
  data->sidemove = sidemove;
  for(i = 0; i < 3; ++i){
    data->v_velocity[i] = SIMD(set1_ps)(velocity[i]);
  }
  data->v_vel_wish_z_square = SIMD(set1_ps)(velocity[2] * wishdir[2]);

  // out's
  data->speed_delta_total = speed_delta_total;
  data->speed_delta_forward = speed_delta_forward;
  data->speed_delta_side = speed_delta_side;
  data->speed_delta_up = speed_delta_up;
  data->speed_delta_plane = speed_delta_plane;
}

static inline SIMD_TYPE SIMD_SUFFIX(vector_length)
(
  const SIMD_TYPE *v_x, const SIMD_TYPE *v_y, const SIMD_TYPE *v_z
){
  // sqrt(x^2 + y^2 + z^2)
  return SIMD(sqrt_ps)(
    SIMD(add_ps)(
      SIMD(add_ps)(
          SIMD(mul_ps)(*v_x, *v_x),
          SIMD(mul_ps)(*v_y, *v_y)
      ),
      SIMD(mul_ps)(*v_z, *v_z)
    )
  );
}

static inline SIMD_TYPE SIMD_SUFFIX(vector_normalize)
(
  SIMD_TYPE *v_x, SIMD_TYPE *v_y, SIMD_TYPE *v_z
){
  SIMD_TYPE v_length = SIMD_SUFFIX(vector_length)(v_x, v_y, v_z);

  // mask to keep originals in case v_length == 0
  SIMD_TYPE v_mask_gtz = SIMD(cmp_ps)(v_length, SIMD(setzero_ps)(), _CMP_GT_OS);

  // ilength = 1.0 / length
  SIMD_TYPE v_ilength = SIMD(div_ps)(SIMD(set1_ps)(1.0f), v_length);

  // normalize: v *= ilength (or original)
  *v_x = SIMD(blendv_ps)(*v_x, SIMD(mul_ps)(*v_x, v_ilength), v_mask_gtz);
  *v_y = SIMD(blendv_ps)(*v_y, SIMD(mul_ps)(*v_y, v_ilength), v_mask_gtz);
  *v_z = SIMD(blendv_ps)(*v_z, SIMD(mul_ps)(*v_z, v_ilength), v_mask_gtz);

  return v_length;
}

static inline void SIMD_SUFFIX(vector_scale)
(
  const SIMD_TYPE *v_x, const SIMD_TYPE *v_y, const SIMD_TYPE *v_z,
  const SIMD_TYPE *v_scale,
  SIMD_TYPE *out_v_x, SIMD_TYPE *out_v_y, SIMD_TYPE *out_v_z
){
  *out_v_x = SIMD(mul_ps)(*v_x, *v_scale);
  *out_v_y = SIMD(mul_ps)(*v_y, *v_scale);
  *out_v_z = SIMD(mul_ps)(*v_z, *v_scale);
}

static inline SIMD_TYPE SIMD_SUFFIX(dot_product)(
  const SIMD_TYPE *v_a_x, const SIMD_TYPE *v_a_y, const SIMD_TYPE *v_a_z,
  const SIMD_TYPE *v_b_x, const SIMD_TYPE *v_b_y, const SIMD_TYPE *v_b_z
){
  // ax * bx + ay * by + az * bz
  return SIMD(add_ps)(
    SIMD(add_ps)(
      SIMD(mul_ps)(*v_a_x, *v_b_x),
      SIMD(mul_ps)(*v_a_y, *v_b_y)
    ),
    SIMD(mul_ps)(*v_a_z, *v_b_z)
  );
}

static inline void SIMD_SUFFIX(snap_vector)(SIMD_TYPE *v_x, SIMD_TYPE *v_y, SIMD_TYPE *v_z)
{
  #if !SIMD_ROUND_SSE2
    *v_x = SIMD(round_ps)(*v_x, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    *v_y = SIMD(round_ps)(*v_y, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    *v_z = SIMD(round_ps)(*v_z, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
  #else
    *v_x = sse2_rint_ps(*v_x);
    *v_y = sse2_rint_ps(*v_y);
    *v_z = sse2_rint_ps(*v_z);
  #endif
}

static inline void SIMD_SUFFIX(clip_velocity)(
    const SIMD_TYPE *v_vel_x, const SIMD_TYPE *v_vel_y, const SIMD_TYPE *v_vel_z,
    const SIMD_TYPE *v_norm_x, const SIMD_TYPE *v_norm_y, const SIMD_TYPE *v_norm_z,
    SIMD_TYPE *out_v_vel_x, SIMD_TYPE *out_v_vel_y, SIMD_TYPE *out_v_vel_z,
    const SIMD_TYPE *v_overbounce)
{
  SIMD_TYPE v_backoff = SIMD_SUFFIX(dot_product)(
    v_vel_x, v_vel_y, v_vel_z,
    v_norm_x, v_norm_y, v_norm_z
  );

  SIMD_TYPE v_mask_backoff_ltz = SIMD(cmp_ps)(v_backoff, SIMD(setzero_ps)(), _CMP_LT_OS);

  // if (dot < 0) -> backoff *= overbounce
  // else -> backoff /= overbounce
  SIMD_TYPE v_backoff_mul = SIMD(mul_ps)(v_backoff, *v_overbounce);
  SIMD_TYPE v_backoff_div = SIMD(div_ps)(v_backoff, *v_overbounce);
  v_backoff = SIMD(blendv_ps)(v_backoff_div, v_backoff_mul, v_mask_backoff_ltz);

  // out = in - normal * backoff
  *out_v_vel_x = SIMD(sub_ps)(*v_vel_x, SIMD(mul_ps)(*v_norm_x, v_backoff));
  *out_v_vel_y = SIMD(sub_ps)(*v_vel_y, SIMD(mul_ps)(*v_norm_y, v_backoff));
  *out_v_vel_z = SIMD(sub_ps)(*v_vel_z, SIMD(mul_ps)(*v_norm_z, v_backoff));
}

static inline void SIMD_SUFFIX(aircontrol)(
  const SIMD_TYPE *v_wishdir_x, const SIMD_TYPE *v_wishdir_y, const SIMD_TYPE *v_wishdir_z,
  SIMD_TYPE *v_vel_io_x, SIMD_TYPE *v_vel_io_y
){
  SIMD_TYPE v_zero = SIMD(setzero_ps)();

  SIMD_TYPE v_speed = SIMD_SUFFIX(vector_normalize)(
    v_vel_io_x, v_vel_io_y, &v_zero // its safe to pass v_zero it wont be modified
  );

  SIMD_TYPE v_dot = SIMD_SUFFIX(dot_product)(
    v_vel_io_x, v_vel_io_y, &v_zero,
    v_wishdir_x, v_wishdir_y, v_wishdir_z
  );

  // k = 32.0f * 150.0f * dot * dot * pm_frametime;
  SIMD_TYPE v_k = SIMD(mul_ps)(
    SIMD(mul_ps)(v_dot, v_dot),
    SIMD(set1_ps)(38.4f) // this one could be reused
  );

  SIMD_TYPE v_mask_dot_gtz = SIMD(cmp_ps)(v_dot, SIMD(setzero_ps)(), _CMP_GT_OS);

  SIMD_TYPE v_new_x = SIMD(add_ps)(
    SIMD(mul_ps)(*v_vel_io_x, v_speed),
    SIMD(mul_ps)(*v_wishdir_x, v_k)
  );

  SIMD_TYPE v_new_y = SIMD(add_ps)(
    SIMD(mul_ps)(*v_vel_io_y, v_speed),
    SIMD(mul_ps)(*v_wishdir_y, v_k)
  );

  SIMD_SUFFIX(vector_normalize)(
    &v_new_x, &v_new_y, &v_zero // its safe to pass v_zero it wont be modified
  );

  // filter dot > 0
  v_new_x = SIMD(blendv_ps)(*v_vel_io_x, v_new_x, v_mask_dot_gtz);
  v_new_y = SIMD(blendv_ps)(*v_vel_io_y, v_new_y, v_mask_dot_gtz);

  *v_vel_io_x = SIMD(mul_ps)(v_new_x, v_speed);
  *v_vel_io_y = SIMD(mul_ps)(v_new_y, v_speed);
}


void SIMD_SUFFIX(calc_speed_delta_walk_worker)(int thread_id, int total_threads, void *job_data)
{
  int i;

  SIMD_SUFFIX(speed_delta_job_t)* job = (SIMD_SUFFIX(speed_delta_job_t)*)job_data;

  // round up
  int chunk_size = (job->resolution + total_threads - 1) / total_threads;
  chunk_size = ((chunk_size + SIMD_WIDTH - 1) / SIMD_WIDTH) * SIMD_WIDTH;

  int start = thread_id * chunk_size;
  int end = start + chunk_size;
  if(end > job->resolution){
    end = job->resolution;
  }

  const SIMD_TYPE v_accelspeed = SIMD(set1_ps)(job->accel * pm_frametime * job->wishspeed);

  for(i = start; i < end; i += SIMD_WIDTH)
  {
    // load trig table
    SIMD_TYPE v_sin = SIMD(loadu_ps)(&job->sin_table[i]);
    SIMD_TYPE v_cos = SIMD(loadu_ps)(&job->cos_table[i]);

    // rotate wishdir
    SIMD_TYPE v_wishdir_rot_x = SIMD(add_ps)(
      SIMD(mul_ps)(v_cos, job->v_wishdir[0]),
      SIMD(mul_ps)(v_sin, job->v_wishdir[1])
    );

    SIMD_TYPE v_wishdir_rot_y = SIMD(sub_ps)(
      SIMD(mul_ps)(v_cos, job->v_wishdir[1]),
      SIMD(mul_ps)(v_sin, job->v_wishdir[0])
    );

    // calc dotproduct(velocity, wishdir rotated)
    SIMD_TYPE v_vel_wish_dot = SIMD(add_ps)(
      SIMD(add_ps)(
        SIMD(mul_ps)(job->v_velocity_sf[0], v_wishdir_rot_x),
        SIMD(mul_ps)(job->v_velocity_sf[1], v_wishdir_rot_y)
      ),
      job->v_vel_wish_z_square_sf // wishdir z is constant
    );

    SIMD_TYPE v_addspeed = SIMD(sub_ps)(job->v_wishspeed, v_vel_wish_dot);

    // mask for addspeed <= 0
    SIMD_TYPE v_addspeed_mask_lez = SIMD(cmp_ps)(v_addspeed, job->v_zero, _CMP_LE_OQ);

    if (SIMD(movemask_ps)(v_addspeed_mask_lez) == 0) {
        // all addspeed are zero -> nothing to calculate
        SIMD(storeu_ps)(&job->speed_delta_total[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_forward[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_side[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_up[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_plane[i], job->v_zero);
        continue;
    }

    SIMD_TYPE v_accelspeed_clamp = SIMD(min_ps)(v_accelspeed, v_addspeed);

    // apply the accelspeed to each velocity axis
    SIMD_TYPE v_velpredict[3];
    v_velpredict[0] = SIMD(add_ps)(
      job->v_velocity_sf[0],
      SIMD(mul_ps)(v_accelspeed_clamp, v_wishdir_rot_x)
    );

    v_velpredict[1] = SIMD(add_ps)(
      job->v_velocity_sf[1],
      SIMD(mul_ps)(v_accelspeed_clamp, v_wishdir_rot_y)
    );

    v_velpredict[2] = SIMD(add_ps)(
      job->v_velocity_sf[2],
      SIMD(mul_ps)(v_accelspeed_clamp, job->v_wishdir[2])
    );

    SIMD_TYPE v_speed = SIMD_SUFFIX(vector_length)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]);

    SIMD_SUFFIX(clip_velocity)(
      &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
      &job->v_normal[0], &job->v_normal[1], &job->v_normal[2],
      &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
      &job->v_overbounce
    );

    SIMD_SUFFIX(vector_normalize)(
      &v_velpredict[0], &v_velpredict[1], &v_velpredict[2]
    );

    SIMD_SUFFIX(vector_scale)(
      &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
      &v_speed,
      &v_velpredict[0], &v_velpredict[1], &v_velpredict[2]
    );

    // snapping
    SIMD_SUFFIX(snap_vector)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]);

    // calc delta
    SIMD_TYPE v_speed_delta_total = SIMD(sub_ps)(
      SIMD_SUFFIX(vector_length)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]),
      job->v_vel_length
    );

    SIMD_TYPE v_speed_delta_forward = SIMD(sub_ps)(
      SIMD_SUFFIX(dot_product)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &v_wishdir_rot_x, &v_wishdir_rot_y, &job->v_wishdir[2]
      ),
      v_vel_wish_dot
    );

    SIMD_TYPE v_speed_delta_up = SIMD(sub_ps)(
      SIMD_SUFFIX(dot_product)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_zero, &job->v_zero, &job->v_one
      ),
      job->v_vel_up_dot
    );

    SIMD_TYPE v_speed_delta_side = SIMD(sub_ps)(
      SIMD(sub_ps)(
        v_speed_delta_total,
        v_speed_delta_forward
      ),
      v_speed_delta_up
    );

    SIMD_TYPE v_speed_delta_plane = SIMD(add_ps)(
      v_speed_delta_forward,
      v_speed_delta_side
    );

    // store results
    SIMD(storeu_ps)(&job->speed_delta_total[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_total, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_forward[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_forward, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_up[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_up, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_side[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_side, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_plane[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_plane, v_addspeed_mask_lez)
    );
  }
}

void SIMD_SUFFIX(calc_speed_delta_air_vq3_worker)(int thread_id, int total_threads, void *job_data)
{
  int i;

  SIMD_SUFFIX(speed_delta_job_t)* job = (SIMD_SUFFIX(speed_delta_job_t)*)job_data;

  // round up
  int chunk_size = (job->resolution + total_threads - 1) / total_threads;
  chunk_size = ((chunk_size + SIMD_WIDTH - 1) / SIMD_WIDTH) * SIMD_WIDTH;

  int start = thread_id * chunk_size;
  int end = start + chunk_size;
  if(end > job->resolution){
    end = job->resolution;
  }

  const SIMD_TYPE v_accelspeed = SIMD(set1_ps)(job->accel * pm_frametime * job->wishspeed);

  for(i = start; i < end; i += SIMD_WIDTH)
  {
    // load trig table
    SIMD_TYPE v_sin = SIMD(loadu_ps)(&job->sin_table[i]);
    SIMD_TYPE v_cos = SIMD(loadu_ps)(&job->cos_table[i]);

    // rotate wishdir
    SIMD_TYPE v_wishdir_rot_x = SIMD(add_ps)(
      SIMD(mul_ps)(v_cos, job->v_wishdir[0]),
      SIMD(mul_ps)(v_sin, job->v_wishdir[1])
    );

    SIMD_TYPE v_wishdir_rot_y = SIMD(sub_ps)(
      SIMD(mul_ps)(v_cos, job->v_wishdir[1]),
      SIMD(mul_ps)(v_sin, job->v_wishdir[0])
    );

    // calc dotproduct(velocity, wishdir rotated)
    SIMD_TYPE v_vel_wish_dot = SIMD(add_ps)(
      SIMD(add_ps)(
        SIMD(mul_ps)(job->v_velocity_sf[0], v_wishdir_rot_x),
        SIMD(mul_ps)(job->v_velocity_sf[1], v_wishdir_rot_y)
      ),
      job->v_vel_wish_z_square_sf // wishdir z is constant
    );

    SIMD_TYPE v_addspeed = SIMD(sub_ps)(job->v_wishspeed, v_vel_wish_dot);

    // mask for addspeed <= 0
    SIMD_TYPE v_addspeed_mask_lez = SIMD(cmp_ps)(v_addspeed, job->v_zero, _CMP_LE_OQ);

    if (SIMD(movemask_ps)(v_addspeed_mask_lez) == 0) {
        // all addspeed are zero -> nothing to calculate
        SIMD(storeu_ps)(&job->speed_delta_total[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_forward[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_side[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_up[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_plane[i], job->v_zero);
        continue;
    }

    SIMD_TYPE v_accelspeed_clamp = SIMD(min_ps)(v_accelspeed, v_addspeed);

    // apply the accelspeed to each velocity axis
    SIMD_TYPE v_velpredict[3];
    v_velpredict[0] = SIMD(add_ps)(
      job->v_velocity_sf[0],
      SIMD(mul_ps)(v_accelspeed_clamp, v_wishdir_rot_x)
    );

    v_velpredict[1] = SIMD(add_ps)(
      job->v_velocity_sf[1],
      SIMD(mul_ps)(v_accelspeed_clamp, v_wishdir_rot_y)
    );

    v_velpredict[2] = SIMD(add_ps)(
      job->v_velocity_sf[2],
      SIMD(mul_ps)(v_accelspeed_clamp, job->v_wishdir[2])
    );

    if(job->air_control){
      SIMD_SUFFIX(aircontrol)(
        &v_wishdir_rot_x, &v_wishdir_rot_y, &job->v_wishdir[2],
        &v_velpredict[0], &v_velpredict[1]
      );
    }

    if(job->ground_plane){
      SIMD_SUFFIX(clip_velocity)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_normal[0], &job->v_normal[1], &job->v_normal[2],
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_overbounce
      );
    }

    // snapping
    SIMD_SUFFIX(snap_vector)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]);

    // calc delta
    SIMD_TYPE v_speed_delta_total = SIMD(sub_ps)(
      SIMD_SUFFIX(vector_length)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]),
      job->v_vel_length
    );

    SIMD_TYPE v_speed_delta_forward = SIMD(sub_ps)(
      SIMD_SUFFIX(dot_product)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &v_wishdir_rot_x, &v_wishdir_rot_y, &job->v_wishdir[2]
      ),
      v_vel_wish_dot
    );

    SIMD_TYPE v_speed_delta_up = SIMD(sub_ps)(
      SIMD_SUFFIX(dot_product)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_zero, &job->v_zero, &job->v_one
      ),
      job->v_vel_up_dot
    );

    SIMD_TYPE v_speed_delta_side = SIMD(sub_ps)(
      SIMD(sub_ps)(
        v_speed_delta_total,
        v_speed_delta_forward
      ),
      v_speed_delta_up
    );

    SIMD_TYPE v_speed_delta_plane = SIMD(add_ps)(
      v_speed_delta_forward,
      v_speed_delta_side
    );

    // store results
    SIMD(storeu_ps)(&job->speed_delta_total[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_total, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_forward[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_forward, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_up[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_up, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_side[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_side, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_plane[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_plane, v_addspeed_mask_lez)
    );
  }
}

void SIMD_SUFFIX(calc_speed_delta_air_cpm_worker)(int thread_id, int total_threads, void *job_data)
{
  int i;
  // these vectors are same for all
  SIMD_TYPE v_accelspeed, v_framewishspeed, v_pm_airaccelerate, v_two_and_half;

  SIMD_SUFFIX(speed_delta_job_t)* job = (SIMD_SUFFIX(speed_delta_job_t)*)job_data;

  // round up
  int chunk_size = (job->resolution + total_threads - 1) / total_threads;
  chunk_size = ((chunk_size + SIMD_WIDTH - 1) / SIMD_WIDTH) * SIMD_WIDTH;

  int start = thread_id * chunk_size;
  int end = start + chunk_size;
  if(end > job->resolution){
    end = job->resolution;
  }

  if(!job->sidemove)
  {
    v_pm_airaccelerate = SIMD(set1_ps)(pm_airaccelerate);
    v_two_and_half = SIMD(set1_ps)(2.5f);
    v_framewishspeed = SIMD(set1_ps)(pm_frametime * job->wishspeed);
  }
  else
  {
    v_accelspeed = SIMD(set1_ps)(job->accel * pm_frametime * job->wishspeed);
  }

  for(i = start; i < end; i += SIMD_WIDTH)
  {
    // load trig table
    SIMD_TYPE v_sin = SIMD(loadu_ps)(&job->sin_table[i]);
    SIMD_TYPE v_cos = SIMD(loadu_ps)(&job->cos_table[i]);

    // rotate wishdir
    SIMD_TYPE v_wishdir_rot_x = SIMD(add_ps)(
      SIMD(mul_ps)(v_cos, job->v_wishdir[0]),
      SIMD(mul_ps)(v_sin, job->v_wishdir[1])
    );

    SIMD_TYPE v_wishdir_rot_y = SIMD(sub_ps)(
      SIMD(mul_ps)(v_cos, job->v_wishdir[1]),
      SIMD(mul_ps)(v_sin, job->v_wishdir[0])
    );

    // calc dotproduct(velocity, wishdir rotated)
    SIMD_TYPE v_vel_wish_dot = SIMD(add_ps)(
      SIMD(add_ps)(
        SIMD(mul_ps)(job->v_velocity_sf[0], v_wishdir_rot_x),
        SIMD(mul_ps)(job->v_velocity_sf[1], v_wishdir_rot_y)
      ),
      job->v_vel_wish_z_square_sf // wishdir z is constant
    );

    SIMD_TYPE v_addspeed = SIMD(sub_ps)(job->v_wishspeed, v_vel_wish_dot);

    // mask for addspeed <= 0
    SIMD_TYPE v_addspeed_mask_lez = SIMD(cmp_ps)(v_addspeed, job->v_zero, _CMP_LE_OQ);

    if (SIMD(movemask_ps)(v_addspeed_mask_lez) == 0) {
        // all addspeed are zero -> nothing to calculate
        SIMD(storeu_ps)(&job->speed_delta_total[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_forward[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_side[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_up[i], job->v_zero);
        SIMD(storeu_ps)(&job->speed_delta_plane[i], job->v_zero);
        continue;
    }

    // special cpm case
    if(!job->sidemove)
    {
      // calc dotproduct(velocity original, wishdir rotated)
      // have to use original because the friction changes it
      SIMD_TYPE v_vel_o_wish_dot = SIMD(add_ps)(
        SIMD(add_ps)(
          SIMD(mul_ps)(job->v_velocity[0], v_wishdir_rot_x),
          SIMD(mul_ps)(job->v_velocity[1], v_wishdir_rot_y)
        ),
        job->v_vel_wish_z_square // wishdir z is constant
      );

      // mask for DotProduct(velocity, wishdir_rotated) < 0
      SIMD_TYPE v_vel_o_wish_dot_mask_ltz = SIMD(cmp_ps)(v_vel_o_wish_dot, job->v_zero, _CMP_LT_OQ);

      // wishdir is rotated -> adjusted accel
      SIMD_TYPE v_accel = SIMD(blendv_ps)(v_pm_airaccelerate, v_two_and_half, v_vel_o_wish_dot_mask_ltz);

      v_accelspeed = SIMD(mul_ps)(v_accel, v_framewishspeed);
    }
    // else -> v_accelspeed is const and precalculated

    SIMD_TYPE v_accelspeed_clamp = SIMD(min_ps)(v_accelspeed, v_addspeed);

    // apply the accelspeed to each velocity axis
    SIMD_TYPE v_velpredict[3];
    v_velpredict[0] = SIMD(add_ps)(
      job->v_velocity_sf[0],
      SIMD(mul_ps)(v_accelspeed_clamp, v_wishdir_rot_x)
    );

    v_velpredict[1] = SIMD(add_ps)(
      job->v_velocity_sf[1],
      SIMD(mul_ps)(v_accelspeed_clamp, v_wishdir_rot_y)
    );

    v_velpredict[2] = SIMD(add_ps)(
      job->v_velocity_sf[2],
      SIMD(mul_ps)(v_accelspeed_clamp, job->v_wishdir[2])
    );

    if(job->air_control){
      SIMD_SUFFIX(aircontrol)(
        &v_wishdir_rot_x, &v_wishdir_rot_y, &job->v_wishdir[2],
        &v_velpredict[0], &v_velpredict[1]
      );
    }

    if(job->ground_plane){
      SIMD_SUFFIX(clip_velocity)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_normal[0], &job->v_normal[1], &job->v_normal[2],
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_overbounce
      );
    }

    // snapping
    SIMD_SUFFIX(snap_vector)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]);

    // calc delta
    SIMD_TYPE v_speed_delta_total = SIMD(sub_ps)(
      SIMD_SUFFIX(vector_length)(&v_velpredict[0], &v_velpredict[1], &v_velpredict[2]),
      job->v_vel_length
    );

    SIMD_TYPE v_speed_delta_forward = SIMD(sub_ps)(
      SIMD_SUFFIX(dot_product)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &v_wishdir_rot_x, &v_wishdir_rot_y, &job->v_wishdir[2]
      ),
      v_vel_wish_dot
    );

    SIMD_TYPE v_speed_delta_up = SIMD(sub_ps)(
      SIMD_SUFFIX(dot_product)(
        &v_velpredict[0], &v_velpredict[1], &v_velpredict[2],
        &job->v_zero, &job->v_zero, &job->v_one
      ),
      job->v_vel_up_dot
    );

    SIMD_TYPE v_speed_delta_side = SIMD(sub_ps)(
      SIMD(sub_ps)(
        v_speed_delta_total,
        v_speed_delta_forward
      ),
      v_speed_delta_up
    );

    SIMD_TYPE v_speed_delta_plane = SIMD(add_ps)(
      v_speed_delta_forward,
      v_speed_delta_side
    );

    // store results
    SIMD(storeu_ps)(&job->speed_delta_total[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_total, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_forward[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_forward, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_up[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_up, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_side[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_side, v_addspeed_mask_lez)
    );
    SIMD(storeu_ps)(&job->speed_delta_plane[i],
      SIMD(blendv_ps)(job->v_zero, v_speed_delta_plane, v_addspeed_mask_lez)
    );
  }
}
