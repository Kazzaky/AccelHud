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
#include "cg_local.h"
#include "cg_utils.h"

#include "cg_syscall.h"

#include "common.h"
#include "q_shared.h"

#define ACCEL_DEBUG 0

#define _ColorClip(a)          ((a)[0] = fmax(fmin((a)[0],1),-1), (a)[1] = fmax(fmin((a)[1],1),-1), (a)[2] = fmax(fmin((a)[2],1),-1), (a)[3] = fmax(fmin((a)[3],1),-1))
#define ColorAdd(a, b, c)      ((c)[0] = (a)[0] + (b)[0], (c)[1] = (a)[1] + (b)[1], (c)[2] = (a)[2] + (b)[2], (c)[3] = (a)[3] + (b)[3], _ColorClip(c))

static vmCvar_t accel;
static vmCvar_t accel_trueness;
static vmCvar_t accel_min_speed;
static vmCvar_t accel_yh;

static vmCvar_t accel_rgba;
static vmCvar_t accel_neg_rgba;
static vmCvar_t accel_hl_rgba;
static vmCvar_t accel_hl_neg_rgba;
static vmCvar_t accel_current_rgba;

static vmCvar_t accel_line_rgba;
static vmCvar_t accel_line_neg_rgba;
static vmCvar_t accel_line_hl_rgba;
static vmCvar_t accel_line_hl_neg_rgba;

static vmCvar_t accel_vline_rgba;
static vmCvar_t accel_zero_rgba;

static vmCvar_t accel_predict_strafe_rgba;
static vmCvar_t accel_predict_sidemove_rgba;
static vmCvar_t accel_predict_opposite_rgba;

static vmCvar_t accel_line_size;
static vmCvar_t accel_vline_size;
static vmCvar_t accel_condensed_size;
static vmCvar_t accel_predict_offset;

static vmCvar_t accel_sidemove;
static vmCvar_t accel_forward;
static vmCvar_t accel_nokey;
static vmCvar_t accel_strafe;
static vmCvar_t accel_opposite;

static vmCvar_t accel_mode_neg;

static vmCvar_t accel_vline;



#if ACCEL_DEBUG
  static vmCvar_t accel_verbose;
#endif // ACCEL_DEBUG


static cvarTable_t accel_cvars[] = {
  { &accel, "p_accel", "0b00000000", CVAR_ARCHIVE_ND },

//#define ACCEL_DISABLED            0
#define ACCEL_ENABLE              1 // the basic view
#define ACCEL_HL_ACTIVE           2 // highlight active
#define ACCEL_LINE_ACTIVE         4 // border line active
#define ACCEL_DISABLE_BAR_AREA    8 // highlight active
#define ACCEL_VL_ACTIVE           16 // vertical line active
#define ACCEL_PL_ACTIVE           32 // point line active
#define ACCEL_CB_ACTIVE           64 // condensed bar active
#define ACCEL_UNIFORM_VALUE       128 // uniform values


  { &accel_trueness, "p_accel_trueness", "0b0000", CVAR_ARCHIVE_ND },

#define ACCEL_TN_JUMPCROUCH      1
#define ACCEL_TN_CPM             2
#define ACCEL_TN_GROUND          4
#define ACCEL_TN_STATIC_BOOST    8 
// 1000 ups -> delta ~1.5f, 3000 ups -> delta ~ 1.0f, 5000 ups -> delta ~ 0.5f

  { &accel_min_speed, "p_accel_min_speed", "0", CVAR_ARCHIVE_ND },
  { &accel_yh, "p_accel_yh", "180 30", CVAR_ARCHIVE_ND },

  { &accel_rgba, "p_accel_rgba", ".2 .9 .2 .6", CVAR_ARCHIVE_ND },
  { &accel_neg_rgba, "p_accel_neg_rgba", ".9 .2 .2 .6", CVAR_ARCHIVE_ND },
  { &accel_hl_rgba, "p_accel_hl_rgba", ".3 1 .3 .9", CVAR_ARCHIVE_ND },
  { &accel_hl_neg_rgba, "p_accel_hl_neg_rgba", ".9 .3 .3 .9", CVAR_ARCHIVE_ND },
  { &accel_current_rgba, "p_accel_cur_rgba", ".9 .1 .9 .9", CVAR_ARCHIVE_ND },
  { &accel_line_rgba, "p_accel_line_rgba", ".5 .9 .5 .8", CVAR_ARCHIVE_ND },
  { &accel_line_neg_rgba, "p_accel_line_neg_rgba", ".9 .5 .5 .8", CVAR_ARCHIVE_ND },
  { &accel_line_hl_rgba, "p_accel_line_hl_rgba", ".6 1 .6 .9", CVAR_ARCHIVE_ND },
  { &accel_line_hl_neg_rgba, "p_accel_line_hl_neg_rgba", "1 .6 .6 .9", CVAR_ARCHIVE_ND },
  { &accel_vline_rgba, "p_accel_vline_rgba", ".1 .1 .9 .7", CVAR_ARCHIVE_ND },
  { &accel_zero_rgba, "p_accel_zero_rgba", ".1 .1 .9 .7", CVAR_ARCHIVE_ND },
  
  { &accel_predict_strafe_rgba, "p_accel_p_strafe_rgbam", "-.2 -.1 .4 -.4", CVAR_ARCHIVE_ND },
  { &accel_predict_sidemove_rgba, "p_accel_p_sm_rgbam", ".4 -.1 -.2 -.4", CVAR_ARCHIVE_ND },
  { &accel_predict_opposite_rgba, "p_accel_p_opposite_rgbam", ".8 .-8 .8 -.3", CVAR_ARCHIVE_ND }, // 1.0 0.9 0.2 0.6
  
  { &accel_line_size, "p_accel_line_size", "5", CVAR_ARCHIVE_ND },
  { &accel_vline_size, "p_accel_vline_size", "1", CVAR_ARCHIVE_ND },
  { &accel_condensed_size, "p_accel_cond_size", "3", CVAR_ARCHIVE_ND },
  { &accel_predict_offset, "p_accel_p_offset", "30", CVAR_ARCHIVE_ND },

  { &accel_sidemove, "p_accel_p_sm", "0", CVAR_ARCHIVE_ND },

//#define ACCEL_AD_DISABLED       0
#define ACCEL_AD_NORMAL         1
#define ACCEL_AD_PREDICT        2
//#define ACCEL_AD_PREDICT_BOTH   3

  { &accel_forward, "p_accel_p_fm", "0", CVAR_ARCHIVE_ND },

//#define ACCEL_FORWARD_DISABLED      0
#define ACCEL_FORWARD_NORMAL        1
#define ACCEL_FORWARD_PREDICT       2

  { &accel_nokey, "p_accel_p_nk", "0", CVAR_ARCHIVE_ND },

//#define ACCEL_NOKEYS_DISABLED      0
#define ACCEL_NOKEY_NORMAL        1
#define ACCEL_NOKEY_PREDICT       2

  { &accel_strafe, "p_accel_p_strafe", "0", CVAR_ARCHIVE_ND },

#define ACCEL_STRAFE_PREDICT       2 // just to make it corelate with other keys

  { &accel_opposite, "p_accel_p_opposite", "0", CVAR_ARCHIVE_ND },

#define ACCEL_OPPOSITE_PREDICT       2 // just to make it corelate with other keys



  { &accel_mode_neg, "p_accel_neg_mode", "0", CVAR_ARCHIVE_ND },

//#define ACCEL_NEG_MODE          0 // negative disabled
#define ACCEL_NEG_MODE_ENABLED    1 // negative enabled
#define ACCEL_NEG_MODE_ADJECENT   2 // only adjecent negative are shown
//#define ACCEL_NEG_MODE_ZERO_ONLY  3 // calculated but only at zero bar shown

  { &accel_vline, "p_accel_vline", "0b00", CVAR_ARCHIVE_ND },

#define ACCEL_VL_USER_COLOR   1 // line have user defined color, if this is not set the colors are pos/neg
#define ACCEL_VL_LINE_H       2 // include bar height

  #if ACCEL_DEBUG
    { &accel_verbose, "p_accel_verbose", "0", CVAR_ARCHIVE_ND },
  #endif // ACCEL_DEBUG
};

enum {
  RGBA_I_POS,
  RGBA_I_NEG,
  RGBA_I_HL_POS,
  RGBA_I_HL_NEG,
  RGBA_I_POINT,
  RGBA_I_BORDER,
  RGBA_I_BORDER_NEG,
  RGBA_I_BORDER_HL_POS,
  RGBA_I_BORDER_HL_NEG,
  RGBA_I_VLINE,
  RGBA_I_ZERO,
  RGBA_I_PREDICT_WAD,
  RGBA_I_PREDICT_AD,
  RGBA_I_PREDICT_OPPOSITE,
  RGBA_I_LENGTH
};

enum {
  PREDICT_NONE,
  PREDICT_SM_STRAFE,
  PREDICT_FMNK_STRAFE,
  PREDICT_FMNK_SM,
  PREDICT_STRAFE_SM,
  PREDICT_SM_STRAFE_ADD, // lets keep this for now
  PREDICT_OPPOSITE
};

// no help here, just because there is no space in the proxymod help table and i don't want to modify it (for now)

typedef struct {
  int   order; // control variable for adjecent checking
  float x; // scaled x 
  float y; // scaled y
  float width; // scaled width
  float value; // raw delta speed
  int   polarity; // 1 = positive, 0 = zero, -1 = negative
} graph_bar;

#define GRAPH_MAX_RESOLUTION 3840 // 4K hardcoded

static graph_bar accel_graph[GRAPH_MAX_RESOLUTION*4]; // regular + prediction other + prediction left + prediction right, i know its insane but for the sake to not overflow 
static int accel_graph_size;

static int resolution;
static int center;
static float x_angle_ratio;
static float ypos_scaled;
static float hud_height_scaled;
static float zero_gap_scaled;
static float line_size_scaled;
static float vline_size_scaled;
static float predict_offset_scaled;

static vec4_t color_tmp;

static int predict;
static int order;

static int velocity_unchanged;

typedef struct
{
  vec2_t        graph_yh;

  vec4_t        graph_rgba[RGBA_I_LENGTH]; 

  pmove_t       pm;
  playerState_t pm_ps;
  pml_t         pml;
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
  accel.integer           = cvar_getInteger("p_accel"); // this is only relevant to be updated before draw
}

static void PmoveSingle(void);
static void PM_AirMove(void);
static void PM_WalkMove(void);

void draw_accel(void)
{
  if (!accel.integer) return;

  // update cvars
  accel_trueness.integer  = cvar_getInteger("p_accel_trueness");
  accel_vline.integer     = cvar_getInteger("p_accel_vline");

  a.pm_ps = *getPs();

  if (VectorLength2(a.pm_ps.velocity) >= accel_min_speed.value) {
    PmoveSingle();
  }

  // TODO: put drawing here
}

static void move(void)
{
  if (a.pml.walking)
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

  if((accel_nokey.value == 0 && !a.pm.cmd.forwardmove && !a.pm.cmd.rightmove)
    || (accel_sidemove.value == 0 && !a.pm.cmd.forwardmove && a.pm.cmd.rightmove)
    || (accel_forward.value == 0 && a.pm.cmd.forwardmove && !a.pm.cmd.rightmove))
  {
    return;
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

  // set mins, maxs, and viewheight
  PM_CheckDuck(&a.pm, &a.pm_ps);

  // set watertype, and waterlevel
  PM_SetWaterLevel(&a.pm, &a.pm_ps);

  // set groundentity
  PM_GroundTrace(&a.pm, &a.pm_ps, &a.pml);

  // note: none of the above (PM_CheckDuck, PM_SetWaterLevel, PM_GroundTrace) use a.pm.cmd.forwardmove a.pm.cmd.rightmove)

  if(a.pm_ps.powerups[PW_FLIGHT] || a.pm_ps.pm_flags & PMF_GRAPPLE_PULL
      || a.pm_ps.pm_flags & PMF_TIME_WATERJUMP || a.pm.waterlevel > 1)
  {
    return;
  }

  // drawing gonna happend

  // update static variables
  ParseVec(accel_yh.string, a.graph_yh, 2);

  resolution = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ? GRAPH_MAX_RESOLUTION : cgs.glconfig.vidWidth;
  center = resolution / 2;
  x_angle_ratio = cg.refdef.fov_x / resolution;
  ypos_scaled = a.graph_yh[0] * cgs.screenXScale;
  hud_height_scaled = a.graph_yh[1] * cgs.screenXScale;
  zero_gap_scaled = accel_condensed_size.value  * cgs.screenXScale;
  line_size_scaled = accel_line_size.value * cgs.screenXScale;
  vline_size_scaled = accel_vline_size.value * cgs.screenXScale;
  predict_offset_scaled = accel_predict_offset.value * cgs.screenXScale;

  predict = 0;
  order = 0; // intentionally here in case we predict both sides to prevent the rare case of adjecent false positive

  signed char key_forwardmove = a.pm.cmd.forwardmove,
      key_rightmove = a.pm.cmd.rightmove;

  // predictions (simulate strafe or a/d)
  if((a.pm_ps.pm_flags & PMF_PROMODE) && accel_sidemove.value == ACCEL_AD_PREDICT && !key_forwardmove && key_rightmove){
    a.pm.cmd.forwardmove = scale;
    a.pm.cmd.rightmove   = scale;
    predict = PREDICT_SM_STRAFE;
    move();
    // opposite side
    a.pm.cmd.rightmove *= -1;
    move();
  }

  if((accel_forward.value == ACCEL_FORWARD_PREDICT && key_forwardmove && !key_rightmove)
      || (accel_nokey.value == ACCEL_NOKEY_PREDICT && !key_forwardmove && !key_rightmove)){
    a.pm.cmd.forwardmove = scale;
    a.pm.cmd.rightmove   = scale;
    predict = PREDICT_FMNK_STRAFE;
    move();
    // opposite side
    a.pm.cmd.rightmove *= -1;
    move();
    // for vq3 additional prediction (case side + forward)
    if(!(a.pm_ps.pm_flags & PMF_PROMODE)){
      predict = PREDICT_FMNK_SM;
      a.pm.cmd.forwardmove = 0;
      a.pm.cmd.rightmove   = scale;
      move();
      a.pm.cmd.rightmove *= -1;
      move();
    }
    return;
  }

  // vq3 specific prediction
  if(!(a.pm_ps.pm_flags & PMF_PROMODE))
  {
    if(accel_sidemove.value == ACCEL_AD_PREDICT && !key_forwardmove && key_rightmove){
      // the strafe predict
      a.pm.cmd.forwardmove = scale;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_SM_STRAFE_ADD;
      move();
      a.pm.cmd.rightmove *= -1;
      move();
    }
    // note: both sidemove and strafe predict can be drawn at the same time
    if(accel_strafe.value == ACCEL_STRAFE_PREDICT && key_forwardmove && key_rightmove){
      // the sidemove predict
      a.pm.cmd.forwardmove = 0;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_STRAFE_SM;
      move();
      a.pm.cmd.rightmove *= -1;
      move();
    }
  }

  // predict same move just opposite side
  if(accel_opposite.value == ACCEL_OPPOSITE_PREDICT)
  {
    // a/d oposite only for vq3
    if(!(a.pm_ps.pm_flags & PMF_PROMODE) && !key_forwardmove && key_rightmove){
      predict = PREDICT_OPPOSITE;
      a.pm.cmd.forwardmove = 0;
      a.pm.cmd.rightmove = key_rightmove * -1;
      move();
    }

    if(key_forwardmove && key_rightmove){
      predict = PREDICT_OPPOSITE;
      a.pm.cmd.forwardmove = scale;
      a.pm.cmd.rightmove = key_rightmove * -1;
      move();
    }
  }

  // restore original keys
  a.pm.cmd.forwardmove = key_forwardmove;
  a.pm.cmd.rightmove   = key_rightmove;

  // reset predict
  predict = PREDICT_NONE;

  // Use default key combination when no user input
  if (accel_nokey.value == ACCEL_NOKEY_NORMAL && !key_forwardmove && !key_rightmove)
  {
    a.pm.cmd.forwardmove = scale;
    a.pm.cmd.rightmove   = scale;
  }

  // regular move
  move();
}

static void set_color_inc_pred(int regular)
{
  switch(predict){
    case PREDICT_OPPOSITE:{
      ColorAdd(a.graph_rgba[regular], a.graph_rgba[RGBA_I_PREDICT_OPPOSITE], color_tmp);
      trap_R_SetColor(color_tmp);
      break;
    }
    case PREDICT_SM_STRAFE:
    case PREDICT_FMNK_STRAFE:
    case PREDICT_SM_STRAFE_ADD:{
      ColorAdd(a.graph_rgba[regular], a.graph_rgba[RGBA_I_PREDICT_WAD], color_tmp);
      trap_R_SetColor(color_tmp);
      break;
    }
    case PREDICT_FMNK_SM:
    case PREDICT_STRAFE_SM:{
      ColorAdd(a.graph_rgba[regular], a.graph_rgba[RGBA_I_PREDICT_AD], color_tmp);
      trap_R_SetColor(color_tmp);
      break;
    }
    case 0:
    default:{
      trap_R_SetColor(a.graph_rgba[regular]);
    }
  }
}

// does not set color
inline static void draw_positive(float x, float y, float w, float h)
{
  if(predict){
    trap_R_DrawStretchPic(x, y - zero_gap_scaled / 2 - predict_offset_scaled, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
  }
  else {
    trap_R_DrawStretchPic(x, y - zero_gap_scaled / 2, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
  }
}

// does not set color
inline static void draw_negative(float x, float y, float w, float h)
{
  trap_R_DrawStretchPic(x, (y - h) + zero_gap_scaled / 2, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

//static void (*draw_polar)(const graph_bar * const, int, float, float);
inline static void _draw_vline(const graph_bar * const bar, int side, float y_offset, float vline_height)
{
  if(bar->polarity > 0)
  {
    set_color_inc_pred(accel_vline.integer & ACCEL_VL_USER_COLOR ? RGBA_I_VLINE : RGBA_I_POS);
    draw_positive(bar->x + (side == 1 ? bar->width - vline_size_scaled : 0), bar->y + bar->polarity * y_offset, (bar->width > vline_size_scaled ? vline_size_scaled : bar->width / 2), vline_height);
  }
  else {
    set_color_inc_pred(accel_vline.integer & ACCEL_VL_USER_COLOR ? RGBA_I_VLINE : RGBA_I_NEG);
    draw_negative(bar->x + (side == 1 ? bar->width - vline_size_scaled : 0), bar->y + bar->polarity * y_offset, (bar->width > vline_size_scaled ? vline_size_scaled : bar->width / 2), vline_height);
  }
}

inline static void vline_to(const graph_bar * const bar, int side, float dist_to_zero)
{
  if(accel_vline.integer & ACCEL_VL_LINE_H)
  {
    _draw_vline(bar, side, 0, dist_to_zero);
  }
  else if(dist_to_zero > line_size_scaled) {
    _draw_vline(bar, side, line_size_scaled, dist_to_zero - line_size_scaled);
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
  int i, j, i_color, negative_draw_skip;
  float y, dist_to_zero, dist_to_adj, height, line_height, speed_delta, cur_speed_delta = 0, angle_yaw_relative, normalizer;
  vec3_t wishdir_rotated;
  graph_bar *bar, *bar_adj;

  float resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
      cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION :
      1.f;

  // update current cvar values
  ParseVec(accel_yh.string, a.graph_yh, 2);
  for (i = 0; i < RGBA_I_LENGTH; ++i) ParseVec(accel_cvars[4 + i].vmCvar->string, a.graph_rgba[i], 4);

  // if(accel.integer & ACCEL_ZEROLINE){
  //   trap_R_SetColor(a.graph_rgba[RGBA_I_ZEROLINE]);
  //   trap_R_DrawStretchPic(0, ypos_scaled, cgs.glconfig.vidWidth, 1 * cgs.screenXScale, 0, 0, 0, 0, cgs.media.whiteShader);
  // }

  // theoretical maximum is: addspeed * sin(45) * 2, but in practice its way less
  // hardcoded for now
  // inacurate approximation
  normalizer = (accel_trueness.integer & ACCEL_TN_STATIC_BOOST ? 2.56f * 1.41421356237f : -1*0.00025f*VectorLength2(a.pm_ps.velocity)+1.75f);
  if(a.pml.walking){
    normalizer *= 15.f;// 38.4f * 1.41421356237f;
  }
  else if (!a.pm.cmd.forwardmove && a.pm.cmd.rightmove && accel_trueness.integer & ACCEL_TN_CPM && a.pm_ps.pm_flags & PMF_PROMODE){
    normalizer *= 11.72f; // 30.f * 1.41421356237f;
  }

  if(!velocity_unchanged){
    // recalculate the graph
    accel_graph_size = 0;
    // cur_speed_delta = 1000.f; // just reset to nonsence value // not needed it imo
    // for each horizontal pixel
    for(i = 0; i < resolution; ++i)
    {
      if(i == center){
        // the current (where the cursor points to)
        cur_speed_delta = speed_delta = calc_accelspeed(wishdir, wishspeed, accel_, 1);
      }
      else{
        // rotating wishdir vector along whole x axis
        angle_yaw_relative = (i - (resolution / 2.f)) * x_angle_ratio;
        VectorCopy(wishdir, wishdir_rotated);
        rotatePointByAngle(wishdir_rotated, angle_yaw_relative);
        speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, accel_, 0);
      }

      // automatically omit negative accel when plotting predictions, also when negatives are disabled ofc
      if((predict || accel_mode_neg.value == 0) && speed_delta <= 0){
        order++; // just control variable to distinct between adjecent bars (checking with x vs x+width is not possible due to float precision false negative)
        continue;
      }

      if(accel_graph_size && accel_graph[accel_graph_size-1].value == speed_delta
          && accel_graph[accel_graph_size-1].order == order - 1) // since we are omitting sometimes this check is mandatory
      {
        accel_graph[accel_graph_size-1].width += resolution_ratio;
      }
      else{
        bar = &(accel_graph[accel_graph_size++]);
        bar->x = i * resolution_ratio;
        bar->polarity = speed_delta ? (speed_delta > 0 ? 1 : -1) : 0;
        bar->y = ypos_scaled + hud_height_scaled * (accel.integer & ACCEL_UNIFORM_VALUE ? bar->polarity : speed_delta / normalizer) * -1;
        bar->width = resolution_ratio;
        bar->value = speed_delta;
        bar->order = order++;
      }
    }

  }
  
  // actual drawing si done here, for each bar in graph
  for(i = 0; i < accel_graph_size; ++i)
  {
    bar = &(accel_graph[i]);
    negative_draw_skip = 0;
    do // dummy loop
    {
      // bar's drawing
      if(bar->polarity > 0){ // positive bar
        if(accel.integer & ACCEL_HL_ACTIVE && bar->value == cur_speed_delta){
          set_color_inc_pred(RGBA_I_HL_POS);
          i_color = RGBA_I_BORDER_HL_POS;
        }
        else {
          set_color_inc_pred(RGBA_I_POS);
          i_color = RGBA_I_BORDER;
        }
        
        height = ypos_scaled-bar->y;
      
        if(accel.integer & ACCEL_LINE_ACTIVE)
        {
          line_height = (height > line_size_scaled ? line_size_scaled : height); // (height - line_size_scaled)
          // if border does not cover whole area
          if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
          {
            // draw uncovered area
            // trap_R_DrawStretchPic(bar->x, bar->y + line_height, bar->width,
            //     height - line_height, 0, 0, 0, 0, cgs.media.whiteShader);
            draw_positive(bar->x, bar->y + line_height, bar->width, height - line_height);
          }
          // draw border line
          set_color_inc_pred(i_color);
          //trap_R_DrawStretchPic(bar->x, bar->y, bar->width, line_height, 0, 0, 0, 0, cgs.media.whiteShader);
          draw_positive(bar->x, bar->y, bar->width, line_height);
        }
        else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA)){
          //trap_R_DrawStretchPic(bar->x, bar->y, bar->width, height, 0, 0, 0, 0, cgs.media.whiteShader);
          draw_positive(bar->x, bar->y, bar->width, height);
        }
      }
      else if(bar->polarity < 0){ // negative bar
        if(accel_mode_neg.value != ACCEL_NEG_MODE_ENABLED && accel_mode_neg.value != ACCEL_NEG_MODE_ADJECENT) continue;
        if((accel_mode_neg.value == ACCEL_NEG_MODE_ADJECENT)
            &&
            // is not positive left adjecent
            !(
              i && accel_graph[i-1].order == bar->order - 1 // check if its rly adjecent // accel_graph[i-1].x + accel_graph[i-1].width == bar->x // (float precision can brake this comparation btw !)
              && accel_graph[i-1].value > 0 // lets not consider 0 as positive in this case
            )
            &&
            // is not positive right adjecent
            !(
              i < accel_graph_size - 1 && accel_graph[i+1].order - 1 == bar->order // check if its rly adjecent // accel_graph[i+1].x == bar->x + bar->width // (float precision can brake this comparation btw !)
              && accel_graph[i+1].value > 0 // lets not consider 0 as positive in this case
            )
          )
        {
          negative_draw_skip = 1;
          continue;
        }

        if(accel.integer & ACCEL_HL_ACTIVE && bar->value == cur_speed_delta)
        {
          set_color_inc_pred(RGBA_I_HL_NEG);
          i_color = RGBA_I_BORDER_HL_NEG;
        }
        else{
          set_color_inc_pred(RGBA_I_NEG);
          i_color = RGBA_I_BORDER_NEG;
        }

        height = (bar->y - ypos_scaled); // height should not be gapped imo need test // + zero_gap_scaled
        
        if(accel.integer & ACCEL_LINE_ACTIVE)
        {
          line_height = (height > line_size_scaled ? line_size_scaled : height);
          // if border does not cover whole area
          if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
          {
            // draw uncovered area
            //trap_R_DrawStretchPic(bar->x, ypos_scaled + zero_gap_scaled, bar->width, height - line_height, 0, 0, 0, 0, cgs.media.whiteShader);  
            draw_negative(bar->x, bar->y - line_height, bar->width, height - line_height);
          }
          set_color_inc_pred(i_color);
          //trap_R_DrawStretchPic(bar->x, ypos_scaled + zero_gap_scaled + (height - line_height), bar->width, line_height, 0, 0, 0, 0, cgs.media.whiteShader);
          draw_negative(bar->x, bar->y, bar->width, line_height);
        }
        else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA)){
          //trap_R_DrawStretchPic(bar->x, ypos_scaled + zero_gap_scaled, bar->width, height, 0, 0, 0, 0, cgs.media.whiteShader);
          draw_negative(bar->x, bar->y, bar->width, height);
        }
      } // zero bars are separated
    } while(0);

    // vertical line's drawing
    if(!negative_draw_skip && bar->value && accel.integer & ACCEL_VL_ACTIVE)
    {
      int skip_no_adjecent;
      // for each side
      for(j = -1; j <= 1; j+= 2)
      {
        skip_no_adjecent = 0;
        // check for adjecent bar
        if(((j < 0 && !i) || (j > 0 && i >= accel_graph_size - 1))
            || accel_graph[i+j].order != bar->order+j) skip_no_adjecent = 1; 

        dist_to_zero = fabs(bar->y - ypos_scaled);

        // current bar do not have an adjecent
        if(skip_no_adjecent)
        {
          // by default lets allow drawing vlines for no adjecent
          vline_to(bar, j, dist_to_zero);
          continue;
        }

        // the case when there is an adjecent bar
        bar_adj = &accel_graph[i+j];

        // check if the adjecents have same polarity
        if(bar->polarity == bar_adj->polarity)
        {
          // adjecent bar have same polarity
          // only case we need to calculate with the adjecent

          // skip vline for lower bar
          if((bar->polarity == 1 && bar->value < bar_adj->value)
              || (bar->polarity == -1 && bar->value > bar_adj->value)) continue;

          dist_to_adj = fabs(bar_adj->y - bar->y);

          vline_to(bar, j, dist_to_adj + (accel_vline.integer & ACCEL_VL_LINE_H ? fmin(dist_to_zero - dist_to_adj, line_size_scaled) : 0));
        }
        else {
          // otherwise we can handle these regulary
          vline_to(bar, j, dist_to_zero);
        }
      }
    } // /vlines
  } // /for each bar

  // zero bars
  if(accel.integer & ACCEL_CB_ACTIVE){
    for(i = 0; i < accel_graph_size; ++i)
    {
      bar = &(accel_graph[i]);
      if(bar->polarity > 0){
        if(accel.integer & ACCEL_HL_ACTIVE && bar->value == cur_speed_delta){
          i_color = RGBA_I_HL_POS;
        }else {
          i_color = RGBA_I_POS;
        }
      }
      else if(bar->polarity < 0){
        if(accel.integer & ACCEL_HL_ACTIVE && bar->value == cur_speed_delta){
          i_color = RGBA_I_HL_NEG;
        }
        else {
          i_color = RGBA_I_NEG;
        }
      }
      else {
        i_color = RGBA_I_ZERO;
      }

      if((predict && bar->polarity > 0) || !predict){
        set_color_inc_pred(i_color);
        draw_positive(bar->x, ypos_scaled, bar->width, zero_gap_scaled);
      }
    }
  }
  // /zero bars

  // point line
  if(!predict && accel.integer & ACCEL_PL_ACTIVE)
  {
    set_color_inc_pred(RGBA_I_POINT);

    y = ypos_scaled + hud_height_scaled * (cur_speed_delta / normalizer) * -1;
    if(cur_speed_delta > 0){
      //trap_R_DrawStretchPic(center - (1 * cgs.screenXScale) / 2, y, 1 * cgs.screenXScale, ypos_scaled - y, 0, 0, 0, 0, cgs.media.whiteShader);
      draw_positive(center - (1 * cgs.screenXScale) / 2, y, 1 * cgs.screenXScale, ypos_scaled - y);
    }
    else if(cur_speed_delta < 0){
      //trap_R_DrawStretchPic(center - (1 * cgs.screenXScale) / 2, ypos_scaled + accel_zero_size.value * cgs.screenXScale, 1 * cgs.screenXScale, (y - ypos_scaled) + accel_zero_size.value  * cgs.screenXScale, 0, 0, 0, 0, cgs.media.whiteShader);
      draw_negative(center - (1 * cgs.screenXScale) / 2, y, 1 * cgs.screenXScale, y - ypos_scaled);
    }
    //trap_R_DrawStretchPic(center - (1 * cgs.screenXScale) / 2, ypos_scaled + hud_height_scaled * (cur_speed_delta / normalizer) * -1, 1 * cgs.screenXScale, line_height_scaled, 0, 0, 0, 0, cgs.media.whiteShader);
  } // /point line

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


  scale = accel_trueness.integer & ACCEL_TN_JUMPCROUCH ?
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
  if ((a.pm_ps.pm_flags & PMF_PROMODE) && (accel_trueness.integer & ACCEL_TN_CPM) && (!a.pm.cmd.forwardmove && a.pm.cmd.rightmove))
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

    scale = accel_trueness.integer & ACCEL_TN_JUMPCROUCH ?
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
  if (accel_trueness.integer & ACCEL_TN_GROUND)
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

