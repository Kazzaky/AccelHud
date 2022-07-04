/*
 * Author: Palme
 * Licence: GPLv3
 *
 * Code heavily use cgame_proxymode and Quake III Arena code,
 * additional licence rules may apply.
 */


#include <stdlib.h>

#include "cg_accel.h"

#include "bg_pmove.h"
#include "cg_cvar.h"
#include "cg_draw.h"
#include "cg_local.h"
#include "cg_utils.h"
#include "help.h"
#include "q_assert.h"
#include "q_math.h"
#include "q_shared.h"
#include "tr_types.h"

#include "cg_syscall.h"

#include "common.h"

#define ACCEL_DEBUG 1

static vmCvar_t accel;
static vmCvar_t accel_trueness;
static vmCvar_t accel_min_speed;
static vmCvar_t accel_yh;
static vmCvar_t accel_rgba;
static vmCvar_t accel_neg_rgba;
static vmCvar_t accel_hl_rgba;
static vmCvar_t accel_hl_neg_rgba;
//static vmCvar_t accel_gap_rgba;
//static vmCvar_t accel_zeroline_rgba;
static vmCvar_t accel_point_rgba;
//static vmCvar_t accel_line_height;
//static vmCvar_t accel_fillgap;
//static vmCvar_t accel_drawzeroline;
static vmCvar_t accel_middle_gap;
#if ACCEL_DEBUG
  static vmCvar_t accel_verbose;
#endif // ACCEL_DEBUG


static cvarTable_t accel_cvars[] = {
  { &accel, "p_accel", "0b111", CVAR_ARCHIVE_ND },
  { &accel_trueness, "p_accel_trueness", "0b110", CVAR_ARCHIVE_ND },
  { &accel_min_speed, "p_accel_min_speed", "0", CVAR_ARCHIVE_ND },
  { &accel_yh, "p_accel_yh", "180 30", CVAR_ARCHIVE_ND },
  { &accel_rgba, "p_accel_rgba", ".2 .9 .2 .6", CVAR_ARCHIVE_ND },
  { &accel_neg_rgba, "p_accel_neg_rgba", ".9 .2 .2 .6", CVAR_ARCHIVE_ND },
  { &accel_hl_rgba, "p_accel_hl_rgba", ".3 1.0 .3 .9", CVAR_ARCHIVE_ND },
  { &accel_hl_neg_rgba, "p_accel_hl_neg_rgba", ".9 .3 .3 .9", CVAR_ARCHIVE_ND },
  //{ &accel_gap_rgba, "p_accel_gap_rgba", ".1 .9 .1 .7", CVAR_ARCHIVE_ND },
  //{ &accel_zeroline_rgba, "p_accel_zeroline_rgba", ".1 .1 .9 .7", CVAR_ARCHIVE_ND },
  { &accel_point_rgba, "p_accel_point_rgba", ".9 .1 .9 .9", CVAR_ARCHIVE_ND },
  //{ &accel_line_height, "p_accel_line_height", "5", CVAR_ARCHIVE_ND },
  //{ &accel_fillgap, "p_accel_fillgap", "1", CVAR_ARCHIVE_ND },
  //{ &accel_drawzeroline, "p_accel_drawzeroline", "1", CVAR_ARCHIVE_ND },
  { &accel_middle_gap, "p_accel_middle_gap", "4", CVAR_ARCHIVE_ND },
  #if ACCEL_DEBUG
    { &accel_verbose, "p_accel_verbose", "0", CVAR_ARCHIVE_ND },
  #endif // ACCEL_DEBUG
};

enum {RGBA_I, RGBA_I_NEG, RGBA_I_HL, RGBA_I_NEG_HL, RGBA_I_POINT};

#define ACCEL_NORMAL    1
#define ACCEL_HL_ACTIVE 2
#define ACCEL_POINT 4

#define ACCEL_JUMPCROUCH 1
#define ACCEL_CPM        2
#define ACCEL_GROUND     4
/*static help_t accel_help[] = {
  {
    accel_cvars + 0,
    BINARY_LITERAL,
    {
      "p_accel 0bXXX",
      "          |||",
      "          ||+- draw basic hud",
      "          |+-- highlight active zone",
      "          +--- show point of current accel",
    },
  },
  {
    accel_cvars + 1,
    BINARY_LITERAL,
    {
      "p_accel_trueness 0bXXX",
      "                   |||",
      "                   ||+- show true jump/crouch zones",
      "                   |+-- show true CPM air control zones",
      "                   +--- show true ground control zones",
    },
  },
  {
    accel_cvars + 3,
    Y | H,
    {
      "p_accel_yh X X",
      "           | |",
      "           | +- y coord of hud center",
      "           +--- height of hud"
    },
  },
  {
    accel_cvars + 4,
    RGBA,
    {
      "p_accel_rgba X X X X",
    },
  },
  {
    accel_cvars + 5,
    RGBA,
    {
      "p_accel_neg_rgba X X X X",
    },
  },
  {
    accel_cvars + 6,
    RGBA,
    {
      "p_accel_hl_rgba X X X X",
    },
  },
  {
    accel_cvars + 7,
    RGBA,
    {
      "p_accel_hl_neg_rgba X X X X",
    },
  },
  {
    accel_cvars + 8,
    RGBA,
    {
      "p_accel_gap_rgba X X X X",
    },
  },
  // {
  //   accel_cvars + 9,
  //   RGBA,
  //   {
  //     "p_accel_zeroline_rgba X X X X",
  //   },
  // },
  {
    accel_cvars + 9,
    RGBA,
    {
      "p_accel_point_rgba X X X X",
    },
  },
};*/

typedef struct {
  float x, y, width,value;
} graph_bar;

#define GRAPH_MAX_RESOLUTION 3840 // 4K hardcoded
static graph_bar accel_graph[GRAPH_MAX_RESOLUTION]; 
static int accel_graph_size;

typedef struct
{
  vec2_t graph_yh;

  vec4_t graph_rgba[5]; 

  pmove_t       pm;
  playerState_t pm_ps;
  pml_t pml;
} accel_t;

static accel_t a;

void init_accel(void)
{
  init_cvars(accel_cvars, ARRAY_LEN(accel_cvars));
  //init_help(accel_help, ARRAY_LEN(accel_help));
}

void update_accel(void)
{
  update_cvars(accel_cvars, ARRAY_LEN(accel_cvars));
  accel.integer          = cvar_getInteger("p_accel");
  accel_trueness.integer = cvar_getInteger("p_accel_trueness");
}

static void PmoveSingle(void);
static void PM_AirMove(void);
static void PM_WalkMove(void);

void draw_accel(void)
{
  if (!accel.integer) return;

  a.pm_ps = *getPs();

  if (VectorLength2(a.pm_ps.velocity) >= accel_min_speed.value) PmoveSingle();
}

static int velocity_unchanged;

static void PmoveSingle(void)
{
  int8_t const scale = a.pm_ps.stats[13] & PSF_USERINPUT_WALK ? 64 : 127;
  if (!cg.demoPlayback && !(a.pm_ps.pm_flags & PMF_FOLLOW))
  {
    int32_t const cmdNum = trap_GetCurrentCmdNumber();
    trap_GetUserCmd(cmdNum, &a.pm.cmd);
  }
  else
  {
    a.pm.cmd.forwardmove = scale * ((a.pm_ps.stats[13] & PSF_USERINPUT_FORWARD) / PSF_USERINPUT_FORWARD -
                                    (a.pm_ps.stats[13] & PSF_USERINPUT_BACKWARD) / PSF_USERINPUT_BACKWARD);
    a.pm.cmd.rightmove   = scale * ((a.pm_ps.stats[13] & PSF_USERINPUT_RIGHT) / PSF_USERINPUT_RIGHT -
                                  (a.pm_ps.stats[13] & PSF_USERINPUT_LEFT) / PSF_USERINPUT_LEFT);
    a.pm.cmd.upmove      = scale * ((a.pm_ps.stats[13] & PSF_USERINPUT_JUMP) / PSF_USERINPUT_JUMP -
                               (a.pm_ps.stats[13] & PSF_USERINPUT_CROUCH) / PSF_USERINPUT_CROUCH);
  }

  // clear all pmove local vars
  memset(&a.pml, 0, sizeof(a.pml));

  velocity_unchanged = VectorCompare(a.pm_ps.velocity, a.pml.previous_velocity);

  // save old velocity for crashlanding
  VectorCopy(a.pm_ps.velocity, a.pml.previous_velocity);

  AngleVectors(a.pm_ps.viewangles, a.pml.forward, a.pml.right, a.pml.up);

  if (a.pm.cmd.upmove < 10)
  {
    // not holding jump
    a.pm_ps.pm_flags &= ~PMF_JUMP_HELD;
  }

  if (a.pm_ps.pm_type >= PM_DEAD)
  {
    a.pm.cmd.forwardmove = 0;
    a.pm.cmd.rightmove   = 0;
    a.pm.cmd.upmove      = 0;
  }

  // Use default key combination when no user input
  if (!a.pm.cmd.forwardmove && !a.pm.cmd.rightmove)
  {
    a.pm.cmd.forwardmove = scale;
    a.pm.cmd.rightmove   = scale;
  }

  // set mins, maxs, and viewheight
  PM_CheckDuck(&a.pm, &a.pm_ps);

  // set watertype, and waterlevel
  PM_SetWaterLevel(&a.pm, &a.pm_ps);

  // set groundentity
  PM_GroundTrace(&a.pm, &a.pm_ps, &a.pml);

  if (a.pm_ps.powerups[PW_FLIGHT])
  {
    // // flight powerup doesn't allow jump and has different friction
    // PM_FlyMove();
    return;
  }
  else if (a.pm_ps.pm_flags & PMF_GRAPPLE_PULL)
  {
    // PM_GrappleMove();
    // // We can wiggle a bit
    // PM_AirMove();
    return;
  }
  else if (a.pm_ps.pm_flags & PMF_TIME_WATERJUMP)
  {
    // PM_WaterJumpMove();
    return;
  }
  else if (a.pm.waterlevel > 1)
  {
    // // swimming
    // PM_WaterMove();
    return;
  }
  else if (a.pml.walking)
  {
    // walking on ground
    PM_WalkMove();
  }
  else
  {
    // airborne
    PM_AirMove();
  }
}


static float calc_accelspeed(const vec3_t wishdir, float const wishspeed, float const accel_, /*vec3_t accel_out,*/ int verbose){
  int			i;
  float		addspeed, accelspeed, currentspeed, accelspeed_delta;
  vec3_t  velpredict;
  (void)verbose;

  vec3_t velocity;
  VectorCopy(a.pm_ps.velocity, velocity);

  Sys_SnapVector(velocity); // solves bug in spectator mode

  #if ACCEL_DEBUG
    if(verbose && accel_verbose.value){
      trap_Print(vaf("velocity[0] = %.5f, velocity[1] = %.5f, velocity[2] = %.5f\n", velocity[0], velocity[1], velocity[2]));
    }
    if(verbose && accel_verbose.value){
      trap_Print(vaf("wishdir[0] = %.5f, wishdir[1] = %.5f, wishdir[2] = %.5f\n", wishdir[0], wishdir[1], wishdir[2]));
    }
  #endif // ACCEL_DEBUG

  currentspeed = DotProduct (velocity, wishdir); // the velocity speed part regardless the wish direction
  #if ACCEL_DEBUG
    if(verbose && accel_verbose.value){
      trap_Print(vaf("accel_ = %.3f, curs = %.3f, wishs = %.3f", accel_, currentspeed, wishspeed));
    }
  #endif // ACCEL_DEBUG
  addspeed = wishspeed - currentspeed;
  #if ACCEL_DEBUG
    if(verbose && accel_verbose.value){
      if (addspeed <= 0) {
        trap_Print(vaf(", adds = %.3f clip !, accs = NaN, delta = NaN\n", addspeed));
      }else{
        trap_Print(vaf(", adds = %.3f", addspeed));
      }
    }
  #endif // ACCEL_DEBUG
  if (addspeed <= 0) {
      return 0;
  }
  accelspeed = accel_*pm_frametime*wishspeed; // fixed pmove
  #if ACCEL_DEBUG
    if(verbose && accel_verbose.value){
      if (accelspeed > addspeed) {
        trap_Print(vaf(", accs = %.3f clip !", accelspeed));
      }else{
        trap_Print(vaf(", accs = %.3f", accelspeed));
      }
    }
  #endif // ACCEL_DEBUG
  if (accelspeed > addspeed) {
      accelspeed = addspeed;
  }

  VectorCopy(velocity, velpredict);
    
  for (i=0 ; i<2 ; i++) {
    velpredict[i] += accelspeed*wishdir[i];
  }

  vec3_t veltmp;
  VectorCopy(velpredict, veltmp);

  // add snapping to predict velocity vector
  Sys_SnapVector(velpredict);
  
  accelspeed_delta = VectorLength2(velpredict) - VectorLength2(velocity);

  #if ACCEL_DEBUG
    if(verbose && accel_verbose.value){
      trap_Print(vaf(", delta = %.5f\n", accelspeed_delta));
    
      trap_Print(vaf("velpredict[0] = %.5f, velpredict[1] = %.5f, velpredict[2] = %.5f\n", velpredict[0], velpredict[1], velpredict[2]));
      trap_Print(vaf("veltmp[0] = %.5f, veltmp[1] = %.5f, veltmp[2] = %.5f\n", veltmp[0], veltmp[1], veltmp[2]));
    }
  #endif //ACCEL_DEBUG

  return accelspeed_delta;
}

static void rotatePointByAngle(vec_t vec[2], float rad){
  vec_t temp[2];
  temp[0] = vec[0];
  temp[1] = vec[1];
  rad*=-1; // + is clockwise | - anticlockwise // yea k now kinda funny w/e :D

  vec[0] = cosf(rad) * temp[0] - sinf(rad) * temp[1];
  vec[1] = sinf(rad) * temp[0] + cosf(rad) * temp[1];
}

/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  int i, resolution = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ? GRAPH_MAX_RESOLUTION : cgs.glconfig.vidWidth;
  float speed_delta, cur_speed_delta = 0, angle_yaw_relative, normalizer,
    resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
      cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION :
      1.f;
  vec3_t wishdir_rotated;
  graph_bar *bar;
  const int center = resolution / 2;
  const float x_angle_ratio = cg.refdef.fov_x / resolution,
              ypos_scaled = a.graph_yh[0] * cgs.screenXScale,
              hud_height_scaled = a.graph_yh[1] * cgs.screenXScale;
              //line_height_scaled = accel_line_height.value * cgs.screenXScale;

  //static graph_bar accel_graph[cgs.glconfig.vidWidth];

  // update current cvar values
  ParseVec(accel_yh.string, a.graph_yh, 2);
  for (i = 0; i < 5; ++i) ParseVec(accel_cvars[4 + i].vmCvar->string, a.graph_rgba[i], 4);

  // if(accel_drawzeroline.value){
  //   trap_R_SetColor(a.graph_rgba[5]);
  //   trap_R_DrawStretchPic(0, ypos_scaled, cgs.glconfig.vidWidth, 2, 0, 0, 0, 0, cgs.media.whiteShader);
  // }

  // theoretical maximum is: addspeed * sin(45) * 2, but in practice its way less
  // hardcoded for now
  normalizer = 2.56f * 1.41421356237f;
  if(a.pml.walking){
    normalizer = 38.4f * 1.41421356237f;
  }
  else if (!a.pm.cmd.forwardmove && a.pm.cmd.rightmove){
    normalizer = 30.f * 1.41421356237f;
  }

  if(!velocity_unchanged){
    accel_graph_size = 0;
    // for each horizontal pixel
    for(i = 0; i < resolution; ++i)
    {
      if(i == center){
        // the current (where the cursor points to)
        cur_speed_delta = speed_delta = calc_accelspeed(wishdir, wishspeed, accel_, 1);
      }
      else{
        angle_yaw_relative = (i - (resolution / 2.f)) * x_angle_ratio;
        VectorCopy(wishdir, wishdir_rotated);
        rotatePointByAngle(wishdir_rotated, angle_yaw_relative);
        speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, accel_, 0);
      }

      if(accel_graph_size && accel_graph[accel_graph_size-1].value == speed_delta){
        accel_graph[accel_graph_size-1].width += resolution_ratio;
      }
      else{
        bar = &(accel_graph[accel_graph_size++]);
        bar->x = i * resolution_ratio;
        bar->y = ypos_scaled + hud_height_scaled * (speed_delta / normalizer) * -1;
        bar->width = resolution_ratio;
        bar->value = speed_delta;
      }
    }
  }
  
  // each bar in graph
  for(i = 0; i < accel_graph_size; ++i)
  {
    bar = &(accel_graph[i]);
    if(bar->value >= 0){
      if(accel.integer & ACCEL_HL_ACTIVE && bar->value == cur_speed_delta){
        trap_R_SetColor(a.graph_rgba[RGBA_I_HL]);
      }
      else{
        trap_R_SetColor(a.graph_rgba[RGBA_I]);
      }
      trap_R_DrawStretchPic(bar->x, bar->y, bar->width, ypos_scaled-bar->y, 0, 0, 0, 0, cgs.media.whiteShader);
    }else{
      if(accel.integer & ACCEL_HL_ACTIVE && bar->value == cur_speed_delta){
        trap_R_SetColor(a.graph_rgba[RGBA_I_NEG_HL]);
      }
      else{
        trap_R_SetColor(a.graph_rgba[RGBA_I_NEG]);
      }
      trap_R_DrawStretchPic(bar->x, ypos_scaled + accel_middle_gap.value * cgs.screenXScale, bar->width, (bar->y - ypos_scaled) + accel_middle_gap.value  * cgs.screenXScale, 0, 0, 0, 0, cgs.media.whiteShader);
    }
    
    // if(accel_fillgap.value){
    //   if(i){
    //     if(accel_graph[i-1].y+line_height_scaled < bar->y){
    //       trap_R_SetColor(a.graph_rgba[4]);
    //       trap_R_DrawStretchPic(bar->x - 1, accel_graph[i-1].y+line_height_scaled, 2, bar->y-accel_graph[i-1].y-line_height_scaled, 0, 0, 0, 0, cgs.media.whiteShader);
    //     }
    //   }
    //   if(i < accel_graph_size - 1){
    //     if(accel_graph[i+1].y+line_height_scaled < bar->y){
    //       trap_R_SetColor(a.graph_rgba[4]);
    //       trap_R_DrawStretchPic(bar->x+bar->width - 1, accel_graph[i+1].y+line_height_scaled, 2, bar->y-accel_graph[i+1].y-line_height_scaled, 0, 0, 0, 0, cgs.media.whiteShader);
    //     }
    //   }
    // }
  }

  if(accel.integer & ACCEL_POINT){
    trap_R_SetColor(a.graph_rgba[RGBA_I_POINT]);
    float y = ypos_scaled + hud_height_scaled * (cur_speed_delta / normalizer) * -1;
    if(cur_speed_delta >= 0){
      trap_R_DrawStretchPic(center - (1 * cgs.screenXScale) / 2, y, 1 * cgs.screenXScale, ypos_scaled - y, 0, 0, 0, 0, cgs.media.whiteShader);
    }
    else{
      trap_R_DrawStretchPic(center - (1 * cgs.screenXScale) / 2, ypos_scaled + accel_middle_gap.value * cgs.screenXScale, 1 * cgs.screenXScale, (y - ypos_scaled) + accel_middle_gap.value  * cgs.screenXScale, 0, 0, 0, 0, cgs.media.whiteShader);
    }
    //trap_R_DrawStretchPic(center - (1 * cgs.screenXScale) / 2, ypos_scaled + hud_height_scaled * (cur_speed_delta / normalizer) * -1, 1 * cgs.screenXScale, line_height_scaled, 0, 0, 0, 0, cgs.media.whiteShader);
  }

  trap_R_SetColor(NULL);
}

static void PM_SlickAccelerate(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  PM_Accelerate(wishdir, wishspeed, accel_);
}

/*
==================
PM_ClipVelocity

Slide off of the impacting surface
==================
*/
static void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
    float	backoff;
    float	change;
    int		i;
    
    backoff = DotProduct (in, normal);
    
    if ( backoff < 0 ) {
        backoff *= overbounce;
    } else {
        backoff /= overbounce;
    }

    for ( i=0 ; i<3 ; i++ ) {
        change = normal[i]*backoff;
        out[i] = in[i] - change;
    }
}


/*
===================
PM_AirMove

===================
*/
static void PM_AirMove( void ) {
  int			i;
  vec3_t		wishvel;
  vec3_t		wishdir;
  float		wishspeed;
  float		scale;


  scale = accel_trueness.integer & ACCEL_JUMPCROUCH ?
    PM_CmdScale(&a.pm_ps, &a.pm.cmd) :
    PM_AltCmdScale(&a.pm_ps, &a.pm.cmd);

  // project moves down to flat plane
  a.pml.forward[2] = 0;
  a.pml.right[2] = 0;
  VectorNormalize (a.pml.forward);
  VectorNormalize (a.pml.right);

  for ( i = 0 ; i < 2 ; i++ ) {
      wishvel[i] = a.pml.forward[i]*a.pm.cmd.forwardmove + a.pml.right[i]*a.pm.cmd.rightmove;
  }
  wishvel[2] = 0;

  VectorCopy (wishvel, wishdir);
  wishspeed = VectorNormalize(wishdir);
  wishspeed *= scale;

  // not on ground, so little effect on velocity
  if ((a.pm_ps.pm_flags & PMF_PROMODE) && (accel_trueness.integer & ACCEL_CPM) && (!a.pm.cmd.forwardmove && a.pm.cmd.rightmove))
  {
    PM_Accelerate(wishdir, wishspeed > cpm_airwishspeed ? cpm_airwishspeed : wishspeed, cpm_airstrafeaccelerate);
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate);
  }
}



/*
===================
PM_WalkMove

===================
*/
static void PM_WalkMove( void ) {
    int			i;
    vec3_t		wishvel;
    vec3_t		wishdir;
    float		wishspeed;
    float		scale;

    if ( a.pm.waterlevel > 2 && DotProduct( a.pml.forward, a.pml.groundTrace.plane.normal ) > 0 ) {
        // begin swimming
        // PM_WaterMove();
        return;
    }

    if (PM_CheckJump(&a.pm, &a.pm_ps, &a.pml)) {
        // jumped away
        if ( a.pm.waterlevel > 1 ) {
            //PM_WaterMove();
        } else {
            PM_AirMove();
        }
        return;
    }

    scale = accel_trueness.integer & ACCEL_JUMPCROUCH ?
    PM_CmdScale(&a.pm_ps, &a.pm.cmd) :
    PM_AltCmdScale(&a.pm_ps, &a.pm.cmd);

    // project moves down to flat plane
    a.pml.forward[2] = 0;
    a.pml.right[2] = 0;

    // project the forward and right directions onto the ground plane
    PM_ClipVelocity (a.pml.forward, a.pml.groundTrace.plane.normal, a.pml.forward, OVERCLIP );
    PM_ClipVelocity (a.pml.right, a.pml.groundTrace.plane.normal, a.pml.right, OVERCLIP );
    //
    VectorNormalize (a.pml.forward); // exactly 1 unit in space facing forward based on cameraview
    VectorNormalize (a.pml.right); // exactly 1 unit in space facing right based on cameraview

    for ( i = 0 ; i < 2 ; i++ ) {
        wishvel[i] = a.pml.forward[i]*a.pm.cmd.forwardmove + a.pml.right[i]*a.pm.cmd.rightmove; // added fractions of direction (the camera once) increased over move (127 run speed)
    }
    // when going up or down slopes the wish velocity should Not be zero // but its value doesnt come from anywhere here so wtf...
    wishvel[2] = 0;

    VectorCopy (wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir); 
    wishspeed *= scale;

    // clamp the speed lower if ducking
    if ( a.pm_ps.pm_flags & PMF_DUCKED ) {
        if ( wishspeed > a.pm_ps.speed * pm_duckScale ) {
            wishspeed = a.pm_ps.speed * pm_duckScale;
        }
    }

    // clamp the speed lower if wading or walking on the bottom
    if ( a.pm.waterlevel ) {
        float	waterScale;

        waterScale = a.pm.waterlevel / 3.0f;
        waterScale = 1.0f - ( 1.0f - pm_swimScale ) * waterScale;
        if ( wishspeed > a.pm_ps.speed * waterScale ) {
            wishspeed = a.pm_ps.speed * waterScale;
        }
    }

  //when a player gets hit, they temporarily lose
  // full control, which allows them to be moved a bit
  if (accel_trueness.integer & ACCEL_GROUND)
  {
    if (a.pml.groundTrace.surfaceFlags & SURF_SLICK || a.pm_ps.pm_flags & PMF_TIME_KNOCKBACK)
    {
      PM_SlickAccelerate(wishdir, wishspeed, a.pm_ps.pm_flags & PMF_PROMODE ? cpm_slickaccelerate : pm_slickaccelerate);
    }
    else
    {
      // don't reset the z velocity for slopes
      // s.pm_ps.velocity[2] = 0;
      PM_Accelerate(wishdir, wishspeed, a.pm_ps.pm_flags & PMF_PROMODE ? cpm_accelerate : pm_accelerate);
    }
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate);
  }
}

