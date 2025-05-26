/*
 * Author: Palme
 * Licence: GPLv3
 *
 * Code heavily use cgame_proxymode and Quake III Arena code,
 * additional licence rules may apply.
 */

 // TODO: refactor aim zone substitution

#include <float.h>
#include <math.h>
#include <stdlib.h>

#include "cg_accel.h"
#include "accel_version.h"

#include "bg_pmove.h"
#include "cg_tcvar.h"
#include "cg_local.h"
#include "cg_utils.h"

#include "common.h"
#include "q_assert.h"
#include "q_shared.h"

#include "cpu_support.h"
#include "thread_pool.h"

#define ACCEL_DEBUG 0

static const vec3_t vec_up = {0.f, 0.f, 1.f};
static const float half_inv = .5f;

// **** cvars ****

static vmCvar_t accel;
static vmCvar_t version;
static vmCvar_t accel_trueness;
static vmCvar_t accel_min_speed;
static vmCvar_t accel_base_height;
static vmCvar_t accel_max_height;
static vmCvar_t accel_yh;
static vmCvar_t accel_neg_mode;

static vmCvar_t accel_neg_offset;
static vmCvar_t accel_vcenter_offset;
static vmCvar_t accel_p_offset;
static vmCvar_t accel_p_cj_offset;

static vmCvar_t accel_p_cj_overdraw;

static vmCvar_t accel_show_move;
static vmCvar_t accel_show_move_vq3;

#define PREDICTION_BIN_LIST \
  X(accel_p_strafe_w_sm) \
  X(accel_p_strafe_w_fm) \
  X(accel_p_strafe_w_nk) \
  X(accel_p_strafe_w_strafe) \
  X(accel_p_cj_w_strafe) \
  X(accel_p_strafe_w_sm_vq3) \
  X(accel_p_strafe_w_fm_vq3) \
  X(accel_p_strafe_w_nk_vq3) \
  X(accel_p_strafe_w_strafe_vq3) \
  X(accel_p_cj_w_strafe_vq3) \
  X(accel_p_cj_w_sm_vq3) \
  X(accel_p_sm_w_sm_vq3) \
  X(accel_p_sm_w_strafe_vq3) \
  X(accel_p_sm_w_fm_vq3) \
  X(accel_p_sm_w_nk_vq3)

// prediction cvars
#define X(n) static vmCvar_t n;
PREDICTION_BIN_LIST
#undef X

// static vmCvar_t accel_threshold; // -0.38 was the biggest seen plasma climb

// hardcode these
// static vmCvar_t accel_window_threshold;
// static vmCvar_t accel_window_grow_limit;

static vmCvar_t accel_merge_threshold; // width threshold not delta !

static vmCvar_t accel_edge;
static vmCvar_t accel_edge_size;
static vmCvar_t accel_edge_min_size;
static vmCvar_t accel_edge_height;
static vmCvar_t accel_edge_min_height;
static vmCvar_t accel_edge_voffset;

static vmCvar_t accel_window_end;
static vmCvar_t accel_window_end_size;
static vmCvar_t accel_window_end_min_size;
static vmCvar_t accel_window_end_height;
static vmCvar_t accel_window_end_min_height;
static vmCvar_t accel_window_end_voffset;

#if ACCEL_DEBUG
  static vmCvar_t accel_verbose;
#endif // ACCEL_DEBUG


// **** colors ****

#define COLOR_LIST \
  /* (cvar variable name, default) */ \
  X(accel_rgba, ".2 .9 .2 .5") \
  X(accel_neg_rgba, ".9 .2 .2 .5") \
  X(accel_near_edge_rgba, ".7 .1 .1 .5") \
  X(accel_far_edge_rgba, ".1 .1 .7 .5") \
  X(accel_hl_rgba, ".3 1 .3 .75") \
  X(accel_hl_neg_rgba, ".9 .3 .3 .75") \
  X(accel_p_strafe_rgbam, "-.2 -.1 .4 -.4") \
  X(accel_p_sidemove_rgbam, ".4 -.1 -.2 -.4") \
  X(accel_p_opposite_rgbam, ".8 .-8 .8 -.3") \
  X(accel_p_cj_rgbam, "1 1 1 1") \
  X(accel_window_end_rgba, ".2 .4 1 .9") \
  X(accel_window_end_hl_rgba, ".3 .6 .9 .9")
  // add new colors here 

// color id enum
#define COLOR_ID(n) COLOR_ID_##n
#define X(n,d) COLOR_ID(n),
enum {
  COLOR_LIST
  COLOR_ID_LENGTH
};
#undef X

  // legacy table, TODO: remove
  // RGBA_I_POS,
  // RGBA_I_NEG,
  // RGBA_I_EDGE_NEAR,
  // RGBA_I_EDGE_FAR,
  // RGBA_I_HL_POS,
  // RGBA_I_HL_NEG,
  // RGBA_I_PREDICT_WAD,
  // RGBA_I_PREDICT_AD,
  // RGBA_I_PREDICT_OPPOSITE,
  // RGBA_I_PREDICT_CROUCHJUMP,
  // RGBA_I_WINDOW_END,
  // RGBA_I_WINDOW_END_HL,


// color cvars
#define X(n,d) static vmCvar_t n;
COLOR_LIST
#undef X

// **** data structs ****

typedef struct
{
  pmove_t       pm;
  playerState_t pm_ps;
  pml_t         pml;

  int8_t        move_scale; // full cmd walk or run (64 or 127)
} game_t;

static game_t game;

typedef struct
{
  float total; // redundant, but convenience
  float forward;
  float side;
  float up;
} speed_delta_t;

inline static int speed_delta_eq(speed_delta_t const *a, speed_delta_t const *b){
  return a->forward == b->forward && a->side == b->side && a->up == b->up; // a->total == b->total is redundant
}


typedef struct graph_bar_
{
  int               ix; // x in max pixels
  int               iwidth; // width in max pixels
  float             pwidth; // percentage
  speed_delta_t     speed_delta; 
  int               polarity; // 1 = positive, 0 = zero, -1 = negative
  // ^ could be determined by LTZ or GTZ from value but this is tradeoff memory vs ops

  float             angle_start; // fov based (left < 0, right > 0), we need this for calculating speed delta, store to save ops
  float             vel_distance_angle; // fov based from bar middle point (left from velocity < 0, right from velocity > 0)

  // bidirectional linked list
  struct graph_bar_ *next;
  struct graph_bar_ *prev;

  // not the most elegant solution, but we got rid of ordering yay ! 
  int               next_is_adj;
  int               prev_is_adj;
} graph_bar;

#define GRAPH_MAX_RESOLUTION 3840 // 4K hardcoded, MUST BE %8==0 !  \
// ^ technically there is no real limitation besides memory block allocation

typedef struct
{
  vec2_t        yh; // y pos, height (not scaled)
  vec4_t        colors[COLOR_ID_LENGTH];

  graph_bar     graph[GRAPH_MAX_RESOLUTION]; // only one graph is plotted at a time
  int           graph_size;           // how much of the ^ array is currently used

  float         speed;
  vec3_t        velocity;             // snapped -> solve spectator bug
  float         vel_angle;            // velocity angle
  float         yaw_angle;            // current yaw angle

  int           resolution;           // atm this is read from game setting
  float         resolution_ratio;     // how much real pixels is equal to 1 max pixel
  float         resolution_ratio_inv; // inverted
  int           resolution_center;    // resolution / 2
  float         x_angle_ratio;        // used to convert x axis size to angle (from real pixels) \
  // note: using x_angle_ratio somewhere else then for angle_yaw_relative usually mean misuse, use angles instead

  // scaled means: in real pixels
  float         hud_ypos_scaled;
  float         hud_height_scaled;
  float         base_height_scaled;
  float         max_height_scaled;

  float         neg_offset_scaled;
  float         vcenter_offset_scaled;

  float         predict_offset_scaled;
  float         predict_crouchjump_offset_scaled;

  float         edge_size_scaled;
  float         edge_min_size_scaled;
  float         edge_height_scaled;
  float         edge_min_height_scaled;
  float         edge_voffset_scaled;

  float         window_end_size_scaled;
  float         window_end_min_size_scaled;
  float         window_end_height_scaled;
  float         window_end_min_height_scaled;
  float         window_end_voffset_scaled;

  // guards
  float         last_fov_x;
  int           last_vid_width;
  float         last_screen_x_scale;

  // control
  int           draw_block;

  // used for optimization
  float         sin_table[GRAPH_MAX_RESOLUTION];
  float         cos_table[GRAPH_MAX_RESOLUTION];

  float         half_fov_x;
  float         half_fov_x_tan_inv;
  float         quarter_fov_x;
  float         quarter_fov_x_tan_inv;
  float         half_screen_width;

  thread_pool_t thread_pool;

  // per frame
  // float         wishdir_rot_x[GRAPH_MAX_RESOLUTION];
  // float         wishdir_rot_y[GRAPH_MAX_RESOLUTION];
  // float         vel_wishdir_rot_dot[GRAPH_MAX_RESOLUTION];
  // float         accel_param[GRAPH_MAX_RESOLUTION];
  float         speed_delta_total[GRAPH_MAX_RESOLUTION];
  float         speed_delta_forward[GRAPH_MAX_RESOLUTION];
  float         speed_delta_side[GRAPH_MAX_RESOLUTION];
  float         speed_delta_up[GRAPH_MAX_RESOLUTION];

} accel_t;

static accel_t a;

// require resolution and fov (x_angle_ratio)
// there is no need to make this super performant since the change of resolution or fov is rare
static void precalc_trig_tables(void)
{
  int   i;
  float angle;
  // for each horizontal pixel
  for(i = 0; i < a.resolution; ++i)
  {
    angle = (i - a.resolution_center) * a.x_angle_ratio; // (left < 0, right > 0)
    a.sin_table[i] = sinf(angle);
    a.cos_table[i] = cosf(angle);
  }
}

// call after cvar init (inc. tracking)
static void a_init(void)
{
  // set guard
  a.last_fov_x = cg.refdef.fov_x;

  // initial
  a.half_fov_x = cg.refdef.fov_x * half_inv;
  a.half_fov_x_tan_inv = 1.f / tanf(a.half_fov_x);
  a.quarter_fov_x = cg.refdef.fov_x / 4;
  a.quarter_fov_x_tan_inv = 1.f / tanf(a.quarter_fov_x);

  // set guard
  a.last_vid_width = cgs.glconfig.vidWidth;

  // initial
  a.half_screen_width = cgs.glconfig.vidWidth * half_inv;

  a.resolution = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ? GRAPH_MAX_RESOLUTION : cgs.glconfig.vidWidth;
  a.resolution_center = a.resolution * half_inv;
  a.resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
      cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION
      : 1.f;
  a.resolution_ratio_inv = 1.f / a.resolution_ratio;

  // use both above
  a.x_angle_ratio = cg.refdef.fov_x / a.resolution;

  // pre calc sin/cos tables
  precalc_trig_tables();

  // set guard
  a.last_screen_x_scale = cgs.screenXScale;

  // initial
  a.hud_ypos_scaled = a.yh[0] * cgs.screenXScale;
  a.hud_height_scaled = a.yh[1] * cgs.screenXScale;

  a.predict_offset_scaled = accel_p_offset.value * cgs.screenXScale;
  a.predict_crouchjump_offset_scaled = accel_p_cj_offset.value * cgs.screenXScale;

  a.edge_size_scaled = accel_edge_size.value * cgs.screenXScale;
  a.edge_min_size_scaled = accel_edge_min_size.value * cgs.screenXScale;
  a.edge_height_scaled = accel_edge_height.value * cgs.screenXScale;
  a.edge_min_height_scaled = accel_edge_min_height.value * cgs.screenXScale;
  a.edge_voffset_scaled = accel_edge_voffset.value * cgs.screenXScale;

  a.window_end_size_scaled = accel_window_end_size.value * cgs.screenXScale;
  a.window_end_min_size_scaled = accel_window_end_min_size.value * cgs.screenXScale;
  a.window_end_height_scaled = accel_window_end_height.value * cgs.screenXScale;
  a.window_end_min_height_scaled = accel_window_end_min_height.value * cgs.screenXScale;
  a.window_end_voffset_scaled = accel_window_end_voffset.value * cgs.screenXScale;
  a.base_height_scaled = accel_base_height.value * cgs.screenXScale;
  a.max_height_scaled = accel_max_height.value * cgs.screenXScale;
  a.neg_offset_scaled = accel_neg_offset.value * cgs.screenXScale;
  a.vcenter_offset_scaled = accel_vcenter_offset.value * cgs.screenXScale;
}

static void update_static(void)
{
  int fov_or_width_change = 0;

  // precheck
  if(a.last_fov_x != cg.refdef.fov_x || a.last_vid_width != cgs.glconfig.vidWidth){
    fov_or_width_change = 1;
  }

  // fox_x guard
  if(a.last_fov_x != cg.refdef.fov_x)
  {
    a.last_fov_x = cg.refdef.fov_x;
    a.half_fov_x = cg.refdef.fov_x * half_inv;
    a.half_fov_x_tan_inv = 1.f / tanf(a.half_fov_x);
    a.quarter_fov_x = cg.refdef.fov_x / 4;
    a.quarter_fov_x_tan_inv = 1.f / tanf(a.quarter_fov_x);
  }

  // vid_width guard
  if(a.last_vid_width != cgs.glconfig.vidWidth)
  {
    a.last_vid_width = cgs.glconfig.vidWidth;
    a.half_screen_width = cgs.glconfig.vidWidth * half_inv;

    a.resolution = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ? GRAPH_MAX_RESOLUTION : cgs.glconfig.vidWidth;
    a.resolution_center = a.resolution * half_inv;
    a.resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
        cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION
        : 1.f;
    a.resolution_ratio_inv = 1.f / a.resolution_ratio;
  }

  if(fov_or_width_change)
  {
    a.x_angle_ratio = cg.refdef.fov_x / a.resolution;
    precalc_trig_tables();
  }

  // x_scale guard
  if(a.last_screen_x_scale != cgs.screenXScale)
  {
    a.last_screen_x_scale = cgs.screenXScale;

    a.hud_ypos_scaled = a.yh[0] * cgs.screenXScale;
    a.hud_height_scaled = a.yh[1] * cgs.screenXScale;

    a.predict_offset_scaled = accel_p_offset.value * cgs.screenXScale;
    a.predict_crouchjump_offset_scaled = accel_p_cj_offset.value * cgs.screenXScale;

    a.edge_size_scaled = accel_edge_size.value * cgs.screenXScale;
    a.edge_min_size_scaled = accel_edge_min_size.value * cgs.screenXScale;
    a.edge_height_scaled = accel_edge_height.value * cgs.screenXScale;
    a.edge_min_height_scaled = accel_edge_min_height.value * cgs.screenXScale;
    a.edge_voffset_scaled = accel_edge_voffset.value * cgs.screenXScale;

    a.window_end_size_scaled = accel_window_end_size.value * cgs.screenXScale;
    a.window_end_min_size_scaled = accel_window_end_min_size.value * cgs.screenXScale;
    a.window_end_height_scaled = accel_window_end_height.value * cgs.screenXScale;
    a.window_end_min_height_scaled = accel_window_end_min_height.value * cgs.screenXScale;
    a.window_end_voffset_scaled = accel_window_end_voffset.value * cgs.screenXScale;
    a.base_height_scaled = accel_base_height.value * cgs.screenXScale;
    a.max_height_scaled = accel_max_height.value * cgs.screenXScale;
    a.neg_offset_scaled = accel_neg_offset.value * cgs.screenXScale;
    a.vcenter_offset_scaled = accel_vcenter_offset.value * cgs.screenXScale;
  }
}


// **** cvar tables ****

// helper to keep cvar names based on the variable name
#define CVAR_EXPAND_NAME(n) n, "p_" #n

static cvarTable_t accel_cvars[] = {
  { &CVAR_EXPAND_NAME(accel), "0b000000", CVAR_ARCHIVE_ND },
  // #define ACCEL_DISABLED            0
  #define ACCEL_ENABLE              1 // the basic view
  #define ACCEL_HL_ACTIVE           (1 << 1) // highlight active
  #define ACCEL_UNIFORM_VALUE       (1 << 2) // uniform values
  #define ACCEL_WINDOW              (1 << 3) // draw only window bar
  #define ACCEL_NEG_UP              (1 << 4) // negatives grow up (not down as default)
  #define ACCEL_VCENTER             (1 << 5) // apply vertical bar centering

  // ^ old features were completely removed
  // can be found at github tag v0.3.1

  { &CVAR_EXPAND_NAME(version), ACCEL_VERSION, CVAR_USERINFO | CVAR_INIT },
  { &CVAR_EXPAND_NAME(accel_trueness), "0b0000", CVAR_ARCHIVE_ND },
  #define ACCEL_TN_JUMPCROUCH      1
  #define ACCEL_TN_CPM             (1 << 1)
  #define ACCEL_TN_GROUND          (1 << 2)
  #define ACCEL_TN_STATIC_BOOST    (1 << 3)
  // 1000 ups -> delta ~1.5f, 3000 ups -> delta ~ 1.0f, 5000 ups -> delta ~ 0.5f

  { &CVAR_EXPAND_NAME(accel_min_speed), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_base_height), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_max_height), "50", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_yh), "180 30", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_neg_mode), "0", CVAR_ARCHIVE_ND },
  //#define ACCEL_NEG_MODE          0 // negative disabled
  #define ACCEL_NEG_MODE_ENABLED    1 // negative enabled
  #define ACCEL_NEG_MODE_ADJECENT   (1 << 1) // only adjecent negative are shown

  { &CVAR_EXPAND_NAME(accel_neg_offset), "15", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_vcenter_offset), "15", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_offset), "30", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_cj_offset), "0", CVAR_ARCHIVE_ND },

  { &CVAR_EXPAND_NAME(accel_p_cj_overdraw), "0", CVAR_ARCHIVE_ND },

  #define X(n,d) { &CVAR_EXPAND_NAME(n), d, CVAR_ARCHIVE_ND },
  COLOR_LIST
  #undef X

  // enable regular accel graph while holding specific keys
  { &CVAR_EXPAND_NAME(accel_show_move), "0b1101", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_show_move_vq3), "0b1111", CVAR_ARCHIVE_ND },
  #define ACCEL_MOVE_STRAFE         1
  #define ACCEL_MOVE_SIDE           (1 << 1)
  #define ACCEL_MOVE_FORWARD        (1 << 2)
  #define ACCEL_MOVE_SIDE_GROUNDED  (1 << 3)

  #define X(n) { &CVAR_EXPAND_NAME(n), "0b00", CVAR_ARCHIVE_ND },
  PREDICTION_BIN_LIST
  #undef X
  #define ACCEL_PREDICT_MOVE          1
  #define ACCEL_PREDICT_MOVE_WINDOW   (1 << 1)

  { &CVAR_EXPAND_NAME(accel_merge_threshold), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge), "0b000000", CVAR_ARCHIVE_ND },
  //#define ACCEL_EDGE_ENABLE 1
  #define ACCEL_EDGE_FULL_SIZE        (1 << 1) // extend to negative (double size + gap)
  #define ACCEL_EDGE_RELATIVE_SIZE    (1 << 2) // making p_accel_edge_size percentage of bar width
  #define ACCEL_EDGE_RELATIVE_HEIGHT  (1 << 3) // making p_accel_edge_height percentage of bar height
  #define ACCEL_EDGE_VCENTER_FORCE    (1 << 4) // forced vcentering
  #define ACCEL_EDGE_VCENTER          (1 << 5) // override regular accel graph vcentering

  { &CVAR_EXPAND_NAME(accel_edge_size), "1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_min_size), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_min_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_voffset), "0", CVAR_ARCHIVE_ND },

  { &CVAR_EXPAND_NAME(accel_window_end), "0b0000000", CVAR_ARCHIVE_ND },
  #define ACCEL_WINDOW_END_HL              (1 << 1)   // highlight
  #define ACCEL_WINDOW_END_RELATIVE_SIZE   (1 << 2)   // making p_accel_aim_zone_size percentage of bar width
  #define ACCEL_WINDOW_END_RELATIVE_HEIGHT (1 << 3)   // making p_accel_aim_zone_height percentage of bar height
  #define ACCEL_WINDOW_END_SUBTRACT        (1 << 4)  // cutoff the "covered" area of window zone
  #define ACCEL_WINDOW_END_VCENTER_FORCE   (1 << 5)  // forced vcentering
  #define ACCEL_WINDOW_END_VCENTER         (1 << 6)  // override regular accel graph vcentering

  { &CVAR_EXPAND_NAME(accel_window_end_size), "10", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_window_end_min_size), "10", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_window_end_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_window_end_min_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_window_end_voffset), "0", CVAR_ARCHIVE_ND },

  #if ACCEL_DEBUG
    { &CVAR_EXPAND_NAME(accel_verbose), "0", CVAR_ARCHIVE_ND },
  #endif // ACCEL_DEBUG
};


// **** cvars tracking **** (noticing changes)

// general callbacks
static void _tcb_binary(trackTableItem const *item, void *_){
  item->vmCvar->integer  = cvar_getInteger(item->name);
}

static void _tcb_vec2(trackTableItem const *item, void *data){
  ParseVec(item->vmCvar->string, (vec_t *)data, 2);
}

static void _tcb_vec4(trackTableItem const *item, void *data){
  ParseVec(item->vmCvar->string, (vec_t *)data, 4);
}

// each of these has to have callback defined
#define TRACK_LIST_SPECIAL \
  X(accel) \
  X(mdd_projection) \
  X(accel_edge) \
  X(accel_window_end)

// binary parsing or vector parsing is good enough reason to track
#define TRACK_LIST_BINARY \
  X(accel_trueness) \
  \
  X(accel_show_move) \
  X(accel_show_move_vq3) \
  \
  PREDICTION_BIN_LIST

#define TRACK_LIST_VEC2 \
  X(accel_yh, &a.yh)

#define X(n,d) X(n, &a.colors[COLOR_ID(n)])
#define TRACK_LIST_VEC4 \
  COLOR_LIST
#undef X

// declaration of special tracking callbacks
#define X(n) static TRACK_CALLBACK(n);
TRACK_LIST_SPECIAL
#undef X


static trackTableItem accel_track_cvars[] = {
  #define X(n) { &CVAR_EXPAND_NAME(n), 0, TRACK_CALLBACK_NAME(n), 0 },
  TRACK_LIST_SPECIAL
  #undef X
  #define X(n) { &CVAR_EXPAND_NAME(n), 0, _tcb_binary, 0 },
  TRACK_LIST_BINARY
  #undef X
  #define X(n,t) { &CVAR_EXPAND_NAME(n), 0, _tcb_vec2, t },
  TRACK_LIST_VEC2
  #undef X
  #define X(n,t) { &CVAR_EXPAND_NAME(n), 0, _tcb_vec4, t },
  TRACK_LIST_VEC4
  #undef X
};

// **** control enums ****

enum {
  PREDICT_NONE,
  PREDICT_SM_STRAFE,
  PREDICT_FMNK_STRAFE,
  PREDICT_FMNK_SM,
  PREDICT_STRAFE_SM,
  PREDICT_SM_STRAFE_ADD, // lets keep this for now
  PREDICT_OPPOSITE,
  PREDICT_CROUCHJUMP
};

enum {
  MOVE_AIR,
  MOVE_AIR_CPM,
  MOVE_WALK,
  MOVE_WALK_SLICK,
};

// no help here, just because there is no space in the proxymod help table and i don't want to modify it (for now)


inline static float angle_short_radial_distance(float a_, float b_);
inline static void draw_positive(float x, float y, float w, float h);
inline static void _draw_positive_nvc(float x, float y, float w, float h);


// **** functions with switching mechanic ****
// usually enable/disable

static void _vertical_center_noop(float *y, float h){
  (void) y, (void) h;
  // intentionally empty
}

static void _vertical_center(float *y, float h){
  *y -= a.vcenter_offset_scaled - h * half_inv; // assume h is always positive (might need abs here)
}

static void (*vertical_center)(float*, float) = _vertical_center_noop;


// this is expensive, isn't there any better way ? like alternative to trap_R_DrawStretchPic which handle that ?
static void _add_projection_x_0(float *x, float *w)
{
  float angle, proj_x, proj_w;

  angle = (*x / a.half_screen_width - 1) * a.half_fov_x;
  proj_x = a.half_screen_width * (1 + tanf(angle) * a.half_fov_x_tan_inv);
  angle = ((*x + *w) / a.half_screen_width - 1) * a.half_fov_x;
  proj_w = (a.half_screen_width * (1 + tanf(angle) * a.half_fov_x_tan_inv)) - proj_x;
   
  *x = proj_x;
  *w = proj_w;
}

static void _add_projection_x_1(float *x, float *w)
{
  (void) x, (void) w;
  // intentionally empty
}

// this is expensive, isn't there any better way ? like alternative to trap_R_DrawStretchPic which handle that ?
static void _add_projection_x_2(float *x, float *w)
{
  float angle, proj_x, proj_w;

  angle = (*x / a.half_screen_width - 1) * a.half_fov_x;
  proj_x = a.half_screen_width * (1 + tanf(angle * half_inv) * a.quarter_fov_x_tan_inv);
  angle = ((*x + *w) / a.half_screen_width - 1) * a.half_fov_x;
  proj_w = (a.half_screen_width * (1 + tanf(angle * half_inv) * a.quarter_fov_x_tan_inv)) - proj_x;

  *x = proj_x;
  *w = proj_w;
}

static void (*add_projection_x)(float*, float*) = _add_projection_x_1;

void (*draw_positive_edge)(float x, float y, float w, float h) = draw_positive;
void (*draw_positive_window_end)(float x, float y, float w, float h) = draw_positive;


// **** special tracking cvars callbacks ****

static TRACK_CALLBACK(accel)
{
  item->vmCvar->integer = cvar_getInteger(item->name);

  // update all related function pointers
  if(item->vmCvar->integer & ACCEL_VCENTER){
    // enable vertical centering
    vertical_center = _vertical_center;
  } else {
    // disable vertical centering
    vertical_center = _vertical_center_noop;
  }
}

static TRACK_CALLBACK(mdd_projection)
{
  switch(item->vmCvar->integer){
    case 0:
      add_projection_x = _add_projection_x_0;
      break;
    case 1:
      add_projection_x = _add_projection_x_1;
      break;
    case 2:
      add_projection_x = _add_projection_x_2;
      break;
  }
}

static TRACK_CALLBACK(accel_edge)
{
  item->vmCvar->integer = cvar_getInteger(item->name);

  // update all related function pointers
  if(item->vmCvar->integer & ACCEL_VCENTER){ // <- TODO
    // enable vertical centering
    draw_positive_edge = draw_positive;
  } else {
    // disable vertical centering
    draw_positive_edge = _draw_positive_nvc;
  }
}

static TRACK_CALLBACK(accel_window_end)
{
  item->vmCvar->integer = cvar_getInteger(item->name);

  // update all related function pointers
  if(item->vmCvar->integer & ACCEL_VCENTER){ // <- TODO
    // enable vertical centering
    draw_positive_window_end = draw_positive;
  } else {
    // disable vertical centering
    draw_positive_window_end = _draw_positive_nvc;
  }
}

inline static void PmoveSingle(void);
inline static void PmoveSingle_update(void);
inline static void PM_AirMove(void);
inline static void PM_WalkMove(void);
inline static void PM_WalkMove_predict(int predict, int window);
inline static void PM_AirMove_predict(int predict, int window);


// **** primary hud functions ****
// following functions (init_accel, update_accel, draw_accel, del_accel)
// are entry points, from these everything else is called

void init_accel(void)
{
  init_cvars(accel_cvars, ARRAY_LEN(accel_cvars));
  init_tcvars(accel_track_cvars, ARRAY_LEN(accel_track_cvars));

  // a struct initialization
  a_init();

  init_cpu_support();

  // thread pool initialization
  // intend to use SIMD that is why physical and not logical
  int use_threads = get_physical_core_count() - 1; // one less
  if(use_threads < 1)
  {
   use_threads = 1; 
  }

  thread_pool_init(&a.thread_pool, use_threads);
}

void del_accel(void)
{
  // TODO: check if this actually gets called
  thread_pool_destroy(&a.thread_pool);
}

void update_accel(void)
{
  // handle main cvar separately to prevent unnecessary overheat in case accel is disabled
  trap_Cvar_Update(accel_cvars[0].vmCvar);
  accel_cvars[0].vmCvar->integer = cvar_getInteger(accel_cvars[0].cvarName);

  if (!accel.integer) return;

  update_cvars(accel_cvars + 1, ARRAY_LEN(accel_cvars) - 1); // skip the first item which is "p_accel"
  update_tcvars(accel_track_cvars, ARRAY_LEN(accel_track_cvars));

  game.pm_ps = *getPs();

  a.speed = VectorLength2(game.pm_ps.velocity);

  if (a.speed >= accel_min_speed.value) {
    PmoveSingle_update();
  }
}

void draw_accel(void)
{
  if (!accel.integer) return;

  if (a.speed >= accel_min_speed.value) {
    PmoveSingle();
  }

  // TODO: put drawing here
}

inline static void move(void)
{
  if (game.pml.walking)
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

inline static void move_predict(int predict, int window)
{
  if (game.pml.walking)
  {
    // walking on ground
    PM_WalkMove_predict(predict, window);
  }
  else
  {
    // airborne
    PM_AirMove_predict(predict, window);
  }
}

inline static void PmoveSingle_update(void)
{
  // scale is full move, the cmd moves could be partials,
  // particals cause flickering -> p_flickfree force full moves
  game.move_scale = game.pm_ps.stats[13] & PSF_USERINPUT_WALK ? 64 : 127;
  if (!cg.demoPlayback && !(game.pm_ps.pm_flags & PMF_FOLLOW))
  {
    trap_GetUserCmd(trap_GetCurrentCmdNumber(), &game.pm.cmd);
  }
  else
  {
    game.pm.cmd.forwardmove = game.move_scale * ((game.pm_ps.stats[13] & PSF_USERINPUT_FORWARD) / PSF_USERINPUT_FORWARD -
                                    (game.pm_ps.stats[13] & PSF_USERINPUT_BACKWARD) / PSF_USERINPUT_BACKWARD);
    game.pm.cmd.rightmove   = game.move_scale * ((game.pm_ps.stats[13] & PSF_USERINPUT_RIGHT) / PSF_USERINPUT_RIGHT -
                                  (game.pm_ps.stats[13] & PSF_USERINPUT_LEFT) / PSF_USERINPUT_LEFT);
    game.pm.cmd.upmove      = game.move_scale * ((game.pm_ps.stats[13] & PSF_USERINPUT_JUMP) / PSF_USERINPUT_JUMP -
                               (game.pm_ps.stats[13] & PSF_USERINPUT_CROUCH) / PSF_USERINPUT_CROUCH);
  }

  // clear all pmove local vars
  memset(&game.pml, 0, sizeof(game.pml));

  // save old velocity for crashlanding
  VectorCopy(game.pm_ps.velocity, game.pml.previous_velocity);

  AngleVectors(game.pm_ps.viewangles, game.pml.forward, game.pml.right, game.pml.up);

  // TODO: why is this here ? (duck ? water level ? ground trace ?)
  if (game.pm.cmd.upmove < 10)
  {
    // not holding jump
    game.pm_ps.pm_flags &= ~PMF_JUMP_HELD;
  }

  if (game.pm_ps.pm_type >= PM_DEAD)
  {
    game.pm.cmd.forwardmove = 0;
    game.pm.cmd.rightmove   = 0;
    game.pm.cmd.upmove      = 0;
  }

  // set mins, maxs, and viewheight
  PM_CheckDuck(&game.pm, &game.pm_ps);

  // set watertype, and waterlevel
  PM_SetWaterLevel(&game.pm, &game.pm_ps);

  // needed for PM_GroundTrace
  game.pm.tracemask = game.pm_ps.pm_type == PM_DEAD ? MASK_PLAYERSOLID & ~CONTENTS_BODY : MASK_PLAYERSOLID;

  // set groundentity
  PM_GroundTrace(&game.pm, &game.pm_ps, &game.pml);

  // note: none of the above (PM_CheckDuck, PM_SetWaterLevel, PM_GroundTrace) uses niether a.pm.cmd.forwardmove and a.pm.cmd.rightmove

  a.draw_block = (game.pm_ps.powerups[PW_FLIGHT] || game.pm_ps.pm_flags & PMF_GRAPPLE_PULL
      || game.pm_ps.pm_flags & PMF_TIME_WATERJUMP || game.pm.waterlevel > 1);

  if(a.draw_block){
    return;
  }

  update_static();

  // these are dynamic, changes every frame
  a.vel_angle = atan2f(game.pm_ps.velocity[1], game.pm_ps.velocity[0]);
  a.yaw_angle = DEG2RAD(game.pm_ps.viewangles[YAW]);

  VectorCopy(game.pm_ps.velocity, a.velocity);
  Sys_SnapVector(a.velocity); // solves bug in spectator mode
}

inline static void PmoveSingle(void)
{
  if(a.draw_block){
    return;
  }

  // drawing gonna happend

  int predict = 0;
  int predict_window = 0;

  // save original keys
  signed char key_forwardmove = game.pm.cmd.forwardmove,
      key_rightmove = game.pm.cmd.rightmove,
      key_upmove = game.pm.cmd.upmove;

  // * predictions *

  int fm_case;

  // cpm
  if(game.pm_ps.pm_flags & PMF_PROMODE)
  {
    if(accel_p_strafe_w_sm.integer && !key_forwardmove && key_rightmove){
      // strafe predict
      game.pm.cmd.forwardmove = game.move_scale;
      game.pm.cmd.rightmove   = key_rightmove;
      predict = PREDICT_SM_STRAFE;
      predict_window = accel_p_strafe_w_sm.integer & ACCEL_PREDICT_MOVE_WINDOW;
      move_predict(predict, predict_window);
      // opposite side
      game.pm.cmd.rightmove *= -1;
      move_predict(predict, predict_window);
    }

    fm_case = accel_p_strafe_w_fm.integer & ACCEL_PREDICT_MOVE && key_forwardmove && !key_rightmove;
    if(fm_case || (accel_p_strafe_w_nk.integer && !key_forwardmove && !key_rightmove)){
      // strafe predict
      game.pm.cmd.forwardmove = game.move_scale;
      game.pm.cmd.rightmove   = game.move_scale;
      predict = PREDICT_FMNK_STRAFE;
      predict_window = fm_case ? accel_p_strafe_w_fm.integer & ACCEL_PREDICT_MOVE_WINDOW : accel_p_strafe_w_nk.integer & ACCEL_PREDICT_MOVE_WINDOW;
      move_predict(predict, predict_window);
      // opposite side
      game.pm.cmd.rightmove *= -1;
      move_predict(predict, predict_window);
      // return; // no longer in use as we have show_move
    }

    // predict same move just opposite side
    if(accel_p_strafe_w_strafe.integer && key_forwardmove && key_rightmove)
    {
      predict_window = accel_p_strafe_w_strafe.integer & ACCEL_PREDICT_MOVE_WINDOW;
      predict = PREDICT_OPPOSITE;
      game.pm.cmd.forwardmove = key_forwardmove;
      game.pm.cmd.rightmove = key_rightmove * -1;
      move_predict(predict, predict_window);
    }
  }
  else // vq3
  {
    if(accel_p_strafe_w_sm_vq3.integer && !key_forwardmove && key_rightmove){
      // strafe predict
      game.pm.cmd.forwardmove = game.move_scale;
      game.pm.cmd.rightmove   = key_rightmove;
      predict = PREDICT_SM_STRAFE_ADD;
      predict_window = accel_p_strafe_w_sm_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
      move_predict(predict, predict_window);
      // opposite side
      game.pm.cmd.rightmove *= -1;
      move_predict(predict, predict_window);
    }

    fm_case = accel_p_strafe_w_fm_vq3.integer & ACCEL_PREDICT_MOVE && key_forwardmove && !key_rightmove;
    if(fm_case || (accel_p_strafe_w_nk_vq3.integer && !key_forwardmove && !key_rightmove)){
      // strafe predict
      game.pm.cmd.forwardmove = game.move_scale;
      game.pm.cmd.rightmove   = game.move_scale;
      predict = PREDICT_FMNK_STRAFE;
      predict_window = fm_case ? accel_p_strafe_w_fm_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW : accel_p_strafe_w_nk_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
      move_predict(predict, predict_window);
      // opposite side
      game.pm.cmd.rightmove *= -1;
      move_predict(predict, predict_window);
    }

    fm_case = accel_p_sm_w_fm_vq3.integer & ACCEL_PREDICT_MOVE && key_forwardmove && !key_rightmove;
    if(fm_case || (accel_p_sm_w_nk_vq3.integer && !key_forwardmove && !key_rightmove)){
      // sidemove predict
      predict_window = fm_case ? accel_p_sm_w_fm_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW : accel_p_sm_w_nk_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
      predict = PREDICT_FMNK_SM;
      game.pm.cmd.forwardmove = 0;
      game.pm.cmd.rightmove   = game.move_scale;
      move_predict(predict, predict_window);
      // opposite side
      game.pm.cmd.rightmove *= -1;
      move_predict(predict, predict_window);
    }

    // predict same move just opposite side
    if(accel_p_strafe_w_strafe_vq3.integer && key_forwardmove && key_rightmove)
    {
      predict_window = accel_p_strafe_w_strafe_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
      predict = PREDICT_OPPOSITE;
      game.pm.cmd.forwardmove = key_forwardmove;
      game.pm.cmd.rightmove = key_rightmove * -1;
      move_predict(predict, predict_window);
    }

    // predict same move just opposite side
    if(accel_p_sm_w_sm_vq3.integer && !key_forwardmove && key_rightmove)
    {
      predict_window = accel_p_sm_w_sm_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
      // a/d oposite only for vq3
      predict = PREDICT_OPPOSITE;
      game.pm.cmd.forwardmove = 0;
      game.pm.cmd.rightmove = key_rightmove * -1;
      move_predict(predict, predict_window);
    }

    if(accel_p_sm_w_strafe_vq3.integer && key_forwardmove && key_rightmove){
      // the sidemove predict
      game.pm.cmd.forwardmove = 0;
      game.pm.cmd.rightmove   = key_rightmove;
      predict = PREDICT_STRAFE_SM;
      predict_window = accel_p_sm_w_strafe_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
      move_predict(predict, predict_window);
      // opposite side
      game.pm.cmd.rightmove *= -1;
      move_predict(predict, predict_window);
    }
  }

  // crouchjump
  if(!accel_p_cj_overdraw.value){
    LABEL_CJ_OVERDRAW: 
    // cpm
    if(game.pm_ps.pm_flags & PMF_PROMODE)
    {
      // predict same move while jumping / crouching 
      if(accel_p_cj_w_strafe.integer && key_forwardmove && key_rightmove)
      {
        predict_window = accel_p_cj_w_strafe.integer & ACCEL_PREDICT_MOVE_WINDOW;
        predict = PREDICT_CROUCHJUMP;
        game.pm.cmd.forwardmove = key_forwardmove;
        game.pm.cmd.rightmove = key_rightmove;
        game.pm.cmd.upmove = game.move_scale;
        move_predict(predict, predict_window);
      }
    }
    else // vq3
    {
      // predict same move while jumping / crouching // following block is doubled with different accel_crouchjump_overdraw after regular move
      if(accel_p_cj_w_strafe_vq3.integer && key_forwardmove && key_rightmove)
      {
        predict_window = accel_p_cj_w_strafe_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
        predict = PREDICT_CROUCHJUMP;
        game.pm.cmd.forwardmove = key_forwardmove;
        game.pm.cmd.rightmove = key_rightmove;
        game.pm.cmd.upmove = game.move_scale;
        move_predict(predict, predict_window);
      }

      // predict same move while jumping / crouching // following block is doubled with different accel_crouchjump_overdraw after regular move
      if(accel_p_cj_w_sm_vq3.integer && !key_forwardmove && key_rightmove)
      {
        predict_window = accel_p_cj_w_sm_vq3.integer & ACCEL_PREDICT_MOVE_WINDOW;
        // a/d only for vq3
        predict = PREDICT_CROUCHJUMP;
        game.pm.cmd.forwardmove = key_forwardmove;
        game.pm.cmd.rightmove = key_rightmove;
        game.pm.cmd.upmove = game.move_scale;
        move_predict(predict, predict_window);
      }
    }

    if(accel_p_cj_overdraw.value){
      // in case we get here by goto overdraw
      return;
    }
  }


  if((key_forwardmove && key_rightmove // strafe
        && (
          (game.pm_ps.pm_flags & PMF_PROMODE && !(accel_show_move.integer & ACCEL_MOVE_STRAFE))
          || (!(game.pm_ps.pm_flags & PMF_PROMODE) && !(accel_show_move_vq3.integer & ACCEL_MOVE_STRAFE))
        ))
      || (!key_forwardmove && key_rightmove // sidemove
        && (
          (game.pm_ps.pm_flags & PMF_PROMODE // cpm
            && (
              (!game.pml.walking && !(accel_show_move.integer & ACCEL_MOVE_SIDE))
              || (game.pml.walking && !(accel_show_move.integer & ACCEL_MOVE_SIDE_GROUNDED))
            )
          )
          || (!(game.pm_ps.pm_flags & PMF_PROMODE) // vq3
            && (
              (!game.pml.walking && !(accel_show_move_vq3.integer & ACCEL_MOVE_SIDE))
              || (game.pml.walking && !(accel_show_move_vq3.integer & ACCEL_MOVE_SIDE_GROUNDED))
            )
          )
        ))
      || (key_forwardmove && !key_rightmove // forwardmove
        && (
          (game.pm_ps.pm_flags & PMF_PROMODE && !(accel_show_move.integer & ACCEL_MOVE_FORWARD))
          || (!(game.pm_ps.pm_flags & PMF_PROMODE) && !(accel_show_move_vq3.integer & ACCEL_MOVE_FORWARD))
        ))
  ){
    if(accel_p_cj_overdraw.value){
      goto LABEL_CJ_OVERDRAW;
    }
    return; // -> regular move is disabled
  }

  // restore original keys
  game.pm.cmd.forwardmove = key_forwardmove;
  game.pm.cmd.rightmove   = key_rightmove;
  game.pm.cmd.upmove      = key_upmove;

  // regular move
  move();
 
  if(accel_p_cj_overdraw.value){
    goto LABEL_CJ_OVERDRAW;
  }
}

// there is basically only one place where this functions is used, why not just move the code there ?
inline static void set_color_pred(int predict)
{
  switch(predict){
    case PREDICT_OPPOSITE:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_opposite_rgbam)]);
      break;
    }
    case PREDICT_SM_STRAFE:
    case PREDICT_FMNK_STRAFE:
    case PREDICT_SM_STRAFE_ADD:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_strafe_rgbam)]);
      break;
    }
    case PREDICT_FMNK_SM:
    case PREDICT_STRAFE_SM:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_sidemove_rgbam)]);
      break;
    }
    case PREDICT_CROUCHJUMP:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_cj_rgbam)]);
      break;
    }
    case 0:
    default:{
      // we are not predicting atm
      ASSERT_TRUE(0);
    }
  }
}

// convenience only
inline static void set_color(int id)
{
  trap_R_SetColor(a.colors[id]);
}


// does not set color
// automatic vertical centering
// do not use for prediction
inline static void draw_positive(float x, float y, float w, float h)
{
  add_projection_x(&x, &w);

  vertical_center(&y, h);

  trap_R_DrawStretchPic(x, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// does not set color
// automatic vertical centering
// ment to be used for predictions -> because of offset
inline static void draw_positive_o(float x, float y, float w, float h, float offset)
{
  add_projection_x(&x, &w);

  vertical_center(&y, h);

  trap_R_DrawStretchPic(x, y + offset, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// (was used only for line mode)
// // does not set color
// // custom vertical centering
// inline static void draw_positive_vc(float x, float y, float w, float h, float vh)
// {
//   add_projection_x(&x, &w);

//   float y_target = y - zero_gap_scaled / 2;

//   if(predict){
//     y_target -= predict == PREDICT_CROUCHJUMP ? predict_crouchjump_offset_scaled : predict_offset_scaled;
//   }

//   if(accel.integer & ACCEL_VCENTER){
//     y_target -= vcenter_offset_scaled - vh / 2; // is h always positive ? might need abs here
//   }

//   trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
// }


// does not set color
// no vertical centering
// used for fetures with its own vcenter settings (edges, window_end)
// should be called indirectly like draw_positive_edge, draw_positive_window_end
inline static void _draw_positive_nvc(float x, float y, float w, float h)
{
  add_projection_x(&x, &w);

  trap_R_DrawStretchPic(x, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// does not set color
// no vertical centering
// used for fetures with its own vcenter settings (edges, window_end)
inline static void draw_positive_o_nvc(float x, float y, float w, float h, float offset)
{
  add_projection_x(&x, &w);

  // if(predict){
  //   y_target -= predict == PREDICT_CROUCHJUMP ? predict_crouchjump_offset_scaled : predict_offset_scaled;
  // }

  trap_R_DrawStretchPic(x, y + offset, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// does not set color
// automatic vertical centering
inline static void draw_negative(float x, float y, float w, float h)
{
  add_projection_x(&x, &w);

  float y_target;

  if(accel.integer & ACCEL_NEG_UP){
    y_target = a.hud_ypos_scaled - a.neg_offset_scaled - h;
  }else{
    y_target = (y - h) + a.neg_offset_scaled;
  }

  if(accel.integer & ACCEL_VCENTER){
    if(accel.integer & ACCEL_NEG_UP){
      y_target -= a.vcenter_offset_scaled - h * half_inv;
    }
    else
    {
      y_target += a.vcenter_offset_scaled - h * half_inv;
    }
  }
  
  trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}


inline static float angle_short_radial_distance(float a_, float b_)
{
    float d = fmodf(b_ - a_ + M_PI, 2 * M_PI) - M_PI;
    return d < -M_PI ? d + 2 * M_PI : d;
}

/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
// TODO: Write assert to assume 0 <= cT <= 1 // what does cT means ?
inline static void PM_Friction(vec3_t velocity_io)
{
  // ignore slope movement
  float const speed = game.pml.walking ? VectorLength2(velocity_io) : VectorLength(velocity_io);

  if (speed < 1)
  {
    velocity_io[0] = 0;
    velocity_io[1] = 0; // allow sinking underwater
    // FIXME: still have z friction underwater?
    return;
  }

  // apply ground friction
  float drop = 0;
  if (
    game.pm.waterlevel <= 1 && game.pml.walking && !(game.pml.groundTrace.surfaceFlags & SURF_SLICK) &&
    !(game.pm_ps.pm_flags & PMF_TIME_KNOCKBACK))
  {
    float const control = speed < pm_stopspeed ? pm_stopspeed : speed;
    drop += control * pm_friction * pm_frametime;
  }

  // apply water friction even if just wading
  if (game.pm.waterlevel)
  {
    drop += speed * (game.pm_ps.pm_flags & PMF_PROMODE ? .5f : pm_waterfriction) * game.pm.waterlevel * pm_frametime;
  }

  // apply flying friction
  if (game.pm_ps.powerups[PW_FLIGHT])
  {
    drop += speed * pm_flightfriction * pm_frametime;
  }

  // this may cause bug in spectator mode
  if (game.pm_ps.pm_type == PM_SPECTATOR)
  {
    drop += speed * pm_spectatorfriction * pm_frametime;
  }

  // scale the velocity
  float newspeed = speed - drop;
  if (newspeed < 0)
  {
    newspeed = 0;
  }
  newspeed /= speed;

  for (uint8_t i = 0; i < 3; ++i) velocity_io[i] *= newspeed;
}


// following function is modified version of function taken from: https://github.com/ETrun/ETrun/blob/43b9e18b8b367b2c864bcfa210415372820dd212/src/game/bg_pmove.c#L839
inline static void PM_Aircontrol(const vec3_t wishdir, vec3_t velocity_io) {
  float zspeed, speed, dot, k;
  int   i;

  // if (!pms.pm.cmd.rightmove || wishspeed == 0.0) {
  // 	return; 
  // }

  zspeed = velocity_io[2];
  velocity_io[2] = 0;
  speed = VectorNormalize(velocity_io);

  dot = DotProduct(velocity_io, wishdir);
  k = 32.0f * 150.0f * dot * dot * pm_frametime;

  if (dot > 0) {
    for (i = 0; i < 2; ++i) {
      velocity_io[i] = velocity_io[i] * speed + wishdir[i] * k;
    }
    VectorNormalize(velocity_io);
  }

  for (i = 0; i < 2; ++i) {
    velocity_io[i] *= speed;
  }
  velocity_io[2] = zspeed;
}


// following function is taken from: https://github.com/ec-/baseq3a/blob/d851fddadf1c2690ac508b8fc0b18bddba3d93d0/code/game/bg_pmove.c#L129
/*
==================
PM_ClipVelocity

Slide off of the impacting surface
==================
*/
inline static void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
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

// the function to calculate speed delta
// does not modify a.pm_ps.velocity
inline static speed_delta_t calc_speed_delta_walk(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  int     i;
  float   addspeed, accelspeed, forwardspeed;
  vec3_t  velpredict;
  vec3_t  velocity;

  speed_delta_t result = {0};
  
  VectorCopy(game.pm_ps.velocity, velocity);

  Sys_SnapVector(velocity); // solves bug in spectator mode

  PM_Friction(velocity);

  forwardspeed = DotProduct(velocity, wishdir); // forward based on wishdir

  addspeed = wishspeed - forwardspeed; // maximum

  if (addspeed <= 0) {
    return result;
  }

  accelspeed = accel_ * pm_frametime * wishspeed; // fixed pmove
  if (accelspeed > addspeed) {
      accelspeed = addspeed;
  }

  VectorCopy(velocity, velpredict);
    
  for (i=0 ; i<3 ; i++) {
    velpredict[i] += accelspeed * wishdir[i];
  }

  float speed = VectorLength(velpredict);

  // clipping
  PM_ClipVelocity(velpredict, game.pml.groundTrace.plane.normal,
      velpredict, OVERCLIP );
      
  VectorNormalize(velpredict);
  VectorScale(velpredict, speed, velpredict);
 
  // add snapping to predict velocity vector
  Sys_SnapVector(velpredict);

  result.total = VectorLength(velpredict) - VectorLength(velocity);
  result.forward = DotProduct(velpredict, wishdir) - forwardspeed;
  result.up = DotProduct(velpredict, vec_up) - DotProduct(velocity, vec_up);
  result.side = result.total - result.forward - result.up;

  return result;
}

// the function to calculate speed delta
// does not modify a.pm_ps.velocity
inline static speed_delta_t calc_speed_delta_air(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  int     i;
  float   addspeed, accelspeed, forwardspeed;
  vec3_t  velpredict;
  vec3_t  velocity;

  speed_delta_t result = {0};
  
  VectorCopy(game.pm_ps.velocity, velocity);

  Sys_SnapVector(velocity); // solves bug in spectator mode

  PM_Friction(velocity);

  forwardspeed = DotProduct(velocity, wishdir); // forward based on wishdir

  addspeed = wishspeed - forwardspeed; // maximum

  if (addspeed <= 0) {
    return result;
  }

  accelspeed = accel_ * pm_frametime * wishspeed; // fixed pmove
  if (accelspeed > addspeed) {
      accelspeed = addspeed;
  }

  VectorCopy(velocity, velpredict);
    
  for (i=0 ; i<3 ; i++) {
    velpredict[i] += accelspeed * wishdir[i];
  }

  // add aircontrol to predict velocity vector
  if(wishspeed && !game.pm.cmd.forwardmove && game.pm.cmd.rightmove) PM_Aircontrol(wishdir, velpredict);

  // clipping
  if(game.pml.groundPlane)
  {
    PM_ClipVelocity(velpredict, game.pml.groundTrace.plane.normal, velpredict, OVERCLIP );
  }

  // add snapping to predict velocity vector
  Sys_SnapVector(velpredict);

  result.total = VectorLength(velpredict) - VectorLength(velocity);
  result.forward = DotProduct(velpredict, wishdir) - forwardspeed;
  result.up = DotProduct(velpredict, vec_up) - DotProduct(velocity, vec_up);
  result.side = result.total - result.forward - result.up;

  return result;
}

inline static void rotate_point_by_angle_cw(vec_t vec[2], float rad){
  vec_t temp[2];
  temp[0] = vec[0];
  temp[1] = vec[1];

  vec[0] = cosf(rad) * temp[0] + sinf(rad) * temp[1];
  vec[1] = -sinf(rad) * temp[0] + cosf(rad) * temp[1];
}

// is not true when angle is equal to edge
inline static int is_angle_within_bar(graph_bar *bar, float angle)
{
  return bar->angle_start < angle && bar->angle_end > angle;
}


/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
inline static void PM_Accelerate(const vec3_t wishdir, float const wishspeed, float const accel_, int move_type, int predict, int predict_window) // TODO: remove move_type and predict -> split this function into versions
{
  int i, i_color, walk, special_air_case;
  float y, height, angle, angle_end, angle_middle, normalizer, yaw_min_distance, yaw_distance, norm_speed;
  speed_delta_t speed_delta;
  vec3_t velocity, wishdir_rotated;
  graph_bar *bar, *bar_tmp, *window_bar, *center_bar;
  graph_bar *it, *start, *start_origin, *end_origin;
  graph_bar *end; // end is included in loop (last valid element)
  int omit;

   window_bar = NULL;
   center_bar = NULL;
   omit = 0;

  walk = move_type == MOVE_WALK || move_type == MOVE_WALK_SLICK;
  special_air_case = move_type == MOVE_AIR_CPM && (!game.pm.cmd.rightmove || game.pm.cmd.forwardmove);
  
  // theoretical maximum is: addspeed * sin(45) * 2, but in practice its way less
  // hardcoded for now
  // inacurate approximation
   norm_speed = a.speed;
  // dynamic normalizer breaks at 7000ups (value goes negative at this point), since this is an approximation, we can't make it bullet proof, hence this hotfix:
  if( norm_speed > 6000){
     norm_speed = 6000;
  }
  // there is side effect of the approximation and that is changing height profile based on speed, which falsefully give impression that accel is actually higher while it isn't
  // the trueness static boost for those who want real (accurate) height
   normalizer = (accel_trueness.integer & ACCEL_TN_STATIC_BOOST ? 2.56f * 1.41421356237f : -0.00025f * norm_speed + 1.75f);
  if(move_type == MOVE_WALK || move_type == MOVE_WALK_SLICK){
     normalizer *= 15.f;// 38.4f * 1.41421356237f;
  }

  // recalculate the graph
  a.graph_size = 0;
  VectorCopy(a.velocity, velocity);
  PM_Friction(velocity);

  // TODO: call fast math

  // for each horizontal pixel
  for(i = 0; i < a.resolution; ++i)
  {
    // we have trig table, no longer need this
    //angle = (i - a.resolution_center) * a.x_angle_ratio; // (left < 0, right > 0)
    
    // rotating wishdir vector along whole x axis
    VectorCopy(wishdir, wishdir_rotated);
    rotate_point_by_angle_cw(wishdir_rotated, angle);

    // calc speed delta
    if(walk)
    {
      speed_delta = calc_speed_delta_walk(wishdir_rotated, wishspeed, accel_);
    } else if(special_air_case){
      // special case wishdir related values need to be recalculated (accel is different)
      if(DotProduct(game.pm_ps.velocity, wishdir_rotated) < 0){
        speed_delta = calc_speed_delta_air(wishdir_rotated, wishspeed, 2.5f); // cpm extra zone
      }else{
        speed_delta = calc_speed_delta_air(wishdir_rotated, wishspeed, pm_airaccelerate);
      }
    } else {
      speed_delta = calc_speed_delta_air(wishdir_rotated, wishspeed, accel_);
    }

    if(
      // automatically omit negative accel when plotting predictions, also when negatives are disabled ofc
      speed_delta.total <= 0 // there are overall more negative bars, but negative bars are usually disabled -> do not predict branch
      && (predict || accel_neg_mode.value == 0)
    ){
      omit = 1;
      continue;
    }

    // grow the previous bar width when same speed_delta
    if(
        __builtin_expect(a.graph_size && speed_delta_eq(&a.graph[a.graph_size-1].speed_delta, &speed_delta), 1)
        && !omit 
    ){
      a.graph[a.graph_size-1].iwidth += 1;
    }
    else{
      bar = &(a.graph[a.graph_size]);
      bar->ix = i; // * a.resolution_ratio;
      bar->polarity = (speed_delta.total > 0) - (speed_delta.total < 0);
      // bar->height = a.hud_height_scaled * (accel.integer & ACCEL_UNIFORM_VALUE ? bar->polarity : speed_delta.total / normalizer);
      // if(accel_base_height.value > 0){
      //   bar->height += a.base_height_scaled * (bar->height > 0 ? 1 : -1); 
      // }
      // if(fabsf(bar->height) > a.max_height_scaled){
      //   bar->height = a.max_height_scaled * (bar->height > 0 ? 1 : -1);
      // }
      //bar->y = a.hud_ypos_scaled + bar->height * -1; // * -1 because of y axis orientation
      bar->iwidth = 1; // a.resolution_ratio;
      bar->speed_delta = speed_delta;
      bar->angle_start = angle;
      if(__builtin_expect(a.graph_size, 1)){
        // set prev and next
        bar->prev = &(a.graph[a.graph_size-1]);
        bar->prev->next = bar;

        // when we create new bar while previous exist 
        // we can determine adjecent
        // if we didn't omited between these two -> they are adjecent
        bar->prev->next_is_adj = !omit;
        bar->prev_is_adj = !omit;
      }

      // we just created new bar -> reset the omit state
      omit = 0;
      ++a.graph_size;
    }

    if(__builtin_expect(i == a.resolution_center, 0)){

    }
  }

  if(__builtin_expect(!a.graph_size, 0)) return;

  // default
  start = &(a.graph[0]);
  end = &(a.graph[a.graph_size-1]);

  // reset edge cases (because no init and array reuse)
  // better to do it here then while creating to save ops
  start->prev = NULL;
  start->prev_is_adj = 0;
  end->next = NULL;
  end->next_is_adj = 0;

  // merge bars
  if(a.graph_size >= 2 && accel_merge_threshold.value)
  {
    int use_prev = 0;
    for(i = 0; i < a.graph_size; ++i){
      bar = &(a.graph[i]);
      if(__builtin_expect(bar->iwidth <= accel_merge_threshold.value, 0)){
        // left most edge case
        // can't move this before loop, these could be rolling
        // can't predict branch either (micro bars are usually edge case)
        if(bar->next && !bar->prev){ 
          bar->next->iwidth += bar->iwidth;
          bar->next->ix = bar->ix;
          start = bar->next;
          start->prev = NULL; // remove the edge bars
        }
        // right most edge case
        // can't move this before loop, we might recreate this case regardless
        // can't predict branch either (micro bars are usually edge case)
        else if(!bar->next && bar->prev){
          bar->prev->iwidth += bar->iwidth;
          end = bar->prev;
          end->next = NULL; // remove the edge bars
        }
        // middle case
        else if(__builtin_expect(!bar->next || !bar->prev, 0)) // in case of merge we are not guaranteed to have both prev/next so check it
        {
          continue;
        }
        use_prev = (fabsf(bar->speed_delta.total - bar->prev->speed_delta.total) < fabsf(bar->speed_delta.total - bar->next->speed_delta.total));
        if(bar->polarity == bar->prev->polarity && (bar->polarity != bar->next->polarity || use_prev)){
          // extend prev bar
          bar->prev->iwidth += bar->iwidth;
        }
        else if(bar->polarity == bar->next->polarity && (bar->polarity != bar->prev->polarity || !use_prev)){
          // move next and extend
          bar->next->iwidth += bar->iwidth;
          bar->next->ix = bar->ix;
        } else {
          // both have opposite polarity -> do not merge
          continue;
        }

        // skip current bar in linked list
        bar->next->prev = bar->prev;
        bar->prev->next = bar->next;
        
        bar->next->prev_is_adj = bar->prev_is_adj;
        bar->prev->next_is_adj = bar->next_is_adj;
      }
    }
  }
  // from now on do not use index loop, iterate instead

  
  yaw_min_distance = 2 * M_PI; // MAX
  for(it = start; it && it != end->next; it = it->next)
  {
    // set angles
    bar = it;
    angle_end = bar->angle_start + (bar->iwidth - 1) * a.x_angle_ratio; // when they are same this is single-pixel-wide bar
    angle_middle = (bar->angle_start + angle_end) * half_inv;
    bar->vel_distance_angle = angle_short_radial_distance(a.vel_angle, a.yaw_angle + angle_middle);

    // determine the window bar (window bar is build-in now)
    if(bar->polarity != 1){ continue; }
    yaw_distance = fabsf(bar->vel_distance_angle);
    if(yaw_min_distance > yaw_distance){
      yaw_min_distance = yaw_distance;
      window_bar = bar;
    }
  }

  // determine the center bar
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;
    if(is_angle_within_bar(bar, 0)){
      center_bar = bar;
      break;
    }
  }
  
  #if ACCEL_DEBUG
  if(accel_verbose.value && center_bar){
    trap_Print(vaf("center_bar->height: %.3f, center_bar->value: %.3f\n", center_bar->height, center_bar->value));
  }
  #endif

  int window_mode = (!predict && accel.integer & ACCEL_WINDOW) || (predict && predict_window);
  // when drawing just window bar skip all other positive (when we do not got window bar, draw full graph as normally -> that never happend)
  if(window_mode && window_bar){
    // except when window_threshold is reached
    // grow positive both sides 
    start = window_bar;
    for(i = 0; i < accel_window_grow_limit.integer; ++i){
      if(!start->prev || start->width > accel_window_threshold.value || start->prev->polarity != 1 || start->order != start->prev->order + 1){
        break;
      }
      start = start->prev; // grow
    }
    end = window_bar;
    for(i = 0; i < accel_window_grow_limit.integer; ++i){
      if(!end->next || end->width > accel_window_threshold.value || end->next->polarity != 1 || end->order != end->next->order - 1){
        break;
      }
      end = end->next; // grow
    }

    // grow negatives on both sides (from whenever we ended)
    while(start->prev){ // no limit here
      if(!start->prev || start->prev->polarity != -1 || start->order != start->prev->order + 1){  // neutral will stop loop that is what we want
        break;
      }
      start = start->prev; // grow
    }
    while(end->next){ // no limit here
      if(!end->next || end->next->polarity != -1 || end->order != end->next->order - 1){  // neutral will stop loop that is what we want
        break;
      }
      end = end->next; // grow
    }
  }

  // start and end are at final stage


  // calculate actual size of window center

  float window_center_size_calculated = 0;
  float window_parts_size = 0;

  // calculate actual size of aim zone

  float aim_zone_size_calculated = 0;
  int aim_zone_on_right_side = 0;

  // only when we have window bar
  if(!predict && window_bar){
    // window center
    if(accel_window_center.integer)
    {
      // relative / absolute size switch
      if(accel_window_center.integer & ACCEL_WINDOW_CENTER_RELATIVE_SIZE){
        window_center_size_calculated = (accel_window_center_size.value / 100) * window_bar->width;
      }
      else{
        window_center_size_calculated = window_center_size_scaled;
      }

      // applying min size
      if(window_center_size_calculated < window_center_min_size_scaled){
        window_center_size_calculated = window_center_min_size_scaled;
      }

      // size check with actual window bar
      if(window_bar->width <= window_center_size_calculated)
      {
        window_center_size_calculated = window_bar->width;
      }

      window_parts_size = (window_bar->width - window_center_size_calculated) * half_inv;
    }
    // aim zone
    if(accel_aim_zone.integer){
      // relative / absolute size switch
      if(accel_aim_zone.integer & ACCEL_AIM_RELATIVE_SIZE){
        aim_zone_size_calculated = (accel_aim_zone_size.value / 100) * window_bar->width;
      }
      else{
        aim_zone_size_calculated = window_end_size_scaled;
      }

      // applying min size
      if(aim_zone_size_calculated < window_end_min_size_scaled){
        aim_zone_size_calculated = window_end_min_size_scaled;
      }

      // size check with actual window bar
      if(window_bar->width <= aim_zone_size_calculated)
      {
        aim_zone_size_calculated = window_bar->width;
      }
      
      // potentially wrong (sign ?)
      if(angle_short_radial_distance(vel_angle, yaw_angle + window_bar->angle_start) < 0){
        aim_zone_on_right_side = 1;
      }
    }
  }

  // *** draw ***
  // actual drawing si done here, for each bar in graph
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;

    do // dummy loop
    {
      // bar's drawing
      if(bar->polarity > 0)  // positive bar
      {
        // set colors
        if(predict){
          set_color_pred(predict);
        } else {
          if(bar == window_bar && (accel.integer & ACCEL_CUSTOM_WINDOW_COL || accel.integer & ACCEL_HL_WINDOW))
          {
            if(accel.integer & ACCEL_HL_WINDOW && center_bar == window_bar){ // && center_bar // unnecessary check
              set_color(RGBA_I_WINDOW_HL);
            } else {
              set_color(RGBA_I_WINDOW);
            }
          } else if(accel.integer & ACCEL_HL_ACTIVE && bar == center_bar){ // && center_bar // unnecessary check
            set_color(RGBA_I_HL_POS);
          }
          // is swap highlight active and the current bar is the swap-to once 
          else if(hightlight_swap && center_bar && (bar == center_bar->next || bar == center_bar->prev) // here used to be index +/- hightlight_swap instead we now use prev/next
            // and the value is greater then current
            && bar->value > center_bar->value
          ){
            set_color(RGBA_I_HL_G_ADJ);
          }
          else if(accel.integer & ACCEL_COLOR_ALTERNATE){
            if(color_alternate){
              set_color(RGBA_I_ALT);
            }else{
              set_color(RGBA_I_POS);
            }
            color_alternate = !color_alternate;
          } else {
            set_color(RGBA_I_POS);
          }
        }
        
        // height = ypos_scaled-bar->y;
        height = bar->height;
      
        // if(accel.integer & ACCEL_LINE_ACTIVE)
        // {
        //   line_height = (height > line_size_scaled ? line_size_scaled : height); // (height - line_size_scaled)
        //   // if border does not cover whole area, draw rest of the bar
        //   if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
        //   {
        //     // draw uncovered area
        //     if(!predict && bar == window_bar){
        //       if(accel_window_center.integer){
        //         // in case we draw window center we need to split the window bar to parts
        //         if(window_parts_size > FLT_EPSILON * FLT_MIN)
        //         {
        //           if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){ // so ugly TODO: refactor
        //             if(aim_zone_size_calculated < window_parts_size){ // one part is cutted
        //               if(aim_zone_on_right_side){
        //                 draw_positive_vc(bar->x, bar->y + line_height, window_parts_size, height - line_height, height);
        //                 draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size - aim_zone_size_calculated, height - line_height, height);
        //               }
        //               else
        //               {
        //                 draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y + line_height, window_parts_size - aim_zone_size_calculated, height - line_height, height);
        //                 draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size, height - line_height, height);
        //               }
        //             }
        //             else // one part is ommited
        //             {
        //               if(aim_zone_size_calculated > window_parts_size + window_center_size_calculated){ // the only part which we draw is cutted
        //                 float cut_size = aim_zone_size_calculated - (window_parts_size + window_center_size_calculated);
        //                 if(aim_zone_on_right_side){
        //                   // cutted left part
        //                   draw_positive_vc(bar->x, bar->y + line_height, window_parts_size - cut_size, height - line_height, height);
        //                 }else{
        //                   // cutted right part
        //                   draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated + cut_size, bar->y + line_height, window_parts_size - cut_size, height - line_height, height);
        //                 }
        //               }
        //               else
        //               {
        //                 if(aim_zone_on_right_side){
        //                   // full left part
        //                   draw_positive_vc(bar->x, bar->y + line_height, window_parts_size, height - line_height, height);
        //                 }else{
        //                   // full right part
        //                   draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size, height - line_height, height);
        //                 }
        //               }
        //             }
        //           }
        //           else // only window center without substration of aim zone
        //           {
        //             draw_positive_vc(bar->x, bar->y + line_height, window_parts_size, height - line_height, height);
        //             draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size, height - line_height, height);
        //           }
        //         }// else entire bar is handled as center so do not draw anything (window center drawing is done separately)
        //       }
        //       else // window center feature is disabled
        //       {
        //         if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){
        //             if(aim_zone_on_right_side){
        //               draw_positive_vc(bar->x, bar->y + line_height, bar->width - aim_zone_size_calculated, height - line_height, height);
        //             }else{
        //               draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y + line_height, bar->width - aim_zone_size_calculated, height - line_height, height);
        //             }
        //         }
        //         else
        //         {
        //           // just regular window/center bar no parts or cuts
        //           draw_positive_vc(bar->x, bar->y + line_height, bar->width, height - line_height, height);
        //         }
        //       }
        //     }
        //     else // either predict or not window/center bar
        //     {
        //       draw_positive_vc(bar->x, bar->y + line_height, bar->width, height - line_height, height);
        //     }
        //   }
        //   // draw border line
        //   set_color_inc_pred(i_color);
        //   if(!predict && bar == window_bar){
        //     if(accel_window_center.integer){
        //       // in case we draw window center we need to split the window bar to parts
        //       if(window_parts_size > FLT_EPSILON * FLT_MIN)
        //       {
        //         if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){ // so ugly TODO: refactor
        //           if(aim_zone_size_calculated < window_parts_size){ // one part is cutted
        //             if(aim_zone_on_right_side){
        //               draw_positive_vc(bar->x, bar->y, window_parts_size, line_height, height);
        //               draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size - aim_zone_size_calculated, line_height, height);
        //             }
        //             else
        //             {
        //               draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y, window_parts_size - aim_zone_size_calculated, line_height, height);
        //               draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, line_height, height);
        //             }
        //           }
        //           else // one part is ommited
        //           {
        //             if(aim_zone_size_calculated > window_parts_size + window_center_size_calculated){ // the only part which we draw is cutted
        //               float cut_size = aim_zone_size_calculated - (window_parts_size + window_center_size_calculated);
        //               if(aim_zone_on_right_side){
        //                 // cutted left part
        //                 draw_positive_vc(bar->x, bar->y, window_parts_size - cut_size, line_height, height);
        //               }else{
        //                 // cutted right part
        //                 draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated + cut_size, bar->y, window_parts_size - cut_size, line_height, height);
        //               }
        //             }
        //             else
        //             {
        //               if(aim_zone_on_right_side){
        //                 // full left part
        //                 draw_positive_vc(bar->x, bar->y, window_parts_size, line_height, height);
        //               }else{
        //                 // full right part
        //                 draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, line_height, height);
        //               }
        //             }
        //           }
        //         }
        //         else // only window center without substration of aim zone
        //         {
        //           draw_positive_vc(bar->x, bar->y, window_parts_size, line_height, height);
        //           draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, line_height, height);
        //         }
        //       } // else entire bar is handled as center so do not draw anything (window center drawing is done separately)
        //     }
        //     else // window center feature is disabled
        //     {
        //       if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){
        //           if(aim_zone_on_right_side){
        //             draw_positive_vc(bar->x, bar->y, bar->width - aim_zone_size_calculated, line_height, height);
        //           }else{
        //             draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y, bar->width + aim_zone_size_calculated, line_height, height);
        //           }
        //       }
        //       else
        //       {
        //         // just regular window/center bar no parts or cuts
        //         draw_positive_vc(bar->x, bar->y, bar->width, line_height, height);
        //       }
        //     }
        //   }
        //   else // either predict or not window/center bar
        //   {
        //     draw_positive_vc(bar->x, bar->y, bar->width, line_height, height);
        //   }
        // }
        // else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
        // ^ cannot be disabled anymore
        {
          if(!predict && bar == window_bar){
            if(accel_window_center.integer){
              if(window_parts_size > FLT_EPSILON * FLT_MIN){
                if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){ // so ugly TODO: refactor
                  if(aim_zone_size_calculated < window_parts_size){ // one part is cutted
                    if(aim_zone_on_right_side){
                      draw_positive(bar->x, bar->y, window_parts_size, height);
                      draw_positive(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size - aim_zone_size_calculated, height);
                    }
                    else
                    {
                      draw_positive(bar->x + aim_zone_size_calculated, bar->y, window_parts_size - aim_zone_size_calculated, height);
                      draw_positive(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, height);
                    }
                  }
                  else // one part is ommited
                  {
                    if(aim_zone_size_calculated > window_parts_size + window_center_size_calculated){ // the only part which we draw is cutted
                      float cut_size = aim_zone_size_calculated - (window_parts_size + window_center_size_calculated);
                      if(aim_zone_on_right_side){
                        // cutted left part
                        draw_positive(bar->x, bar->y, window_parts_size - cut_size, height);
                      }else{
                        // cutted right part
                        draw_positive(bar->x + window_parts_size + window_center_size_calculated + cut_size, bar->y, window_parts_size - cut_size, height);
                      }
                    }
                    else
                    {
                      if(aim_zone_on_right_side){
                        // full left part
                        draw_positive(bar->x, bar->y, window_parts_size, height);
                      }else{
                        // full right part
                        draw_positive(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, height);
                      }
                    }
                  }
                }
                else // only window center without substration of aim zone
                {
                  draw_positive(bar->x, bar->y, window_parts_size, height);
                  draw_positive(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, height);
                }
              }// else entire bar is handled as center so do not draw anything (window center drawing is done separately)
            }
            else // window center feature is disabled
            {
              if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){
                  if(aim_zone_on_right_side){
                    draw_positive(bar->x, bar->y, bar->width - aim_zone_size_calculated, height);
                  }else{
                    draw_positive(bar->x + aim_zone_size_calculated, bar->y, bar->width - aim_zone_size_calculated, height);
                  }
              }
              else
              {
                // just regular window/center bar no parts or cuts
                draw_positive(bar->x, bar->y, bar->width, height);
              }
            }
          }
          else  // either predict or not window/center bar
          {
            draw_positive(bar->x, bar->y, bar->width, height);
          }
        }
      }
      else if(bar->polarity < 0){ // negative bar
        if(accel_neg_mode.value != ACCEL_NEG_MODE_ENABLED && accel_neg_mode.value != ACCEL_NEG_MODE_ADJECENT) continue;
        if((accel_neg_mode.value == ACCEL_NEG_MODE_ADJECENT)
            &&
            // is not positive left adjecent
            !(
              bar->prev && bar->prev->order == bar->order - 1 // check if its rly adjecent
              && bar->prev->value > 0 // lets not consider 0 as positive in this case
            )
            &&
            // is not positive right adjecent
            !(
              bar->next && bar->next->order - 1 == bar->order // check if its rly adjecent
              && bar->next->value > 0 // lets not consider 0 as positive in this case
            )
          )
        {
          // negative_draw_skip = 1;
          continue;
        }

        if(accel.integer & ACCEL_HL_ACTIVE && center_bar && bar == center_bar)
        {
          set_color_inc_pred(RGBA_I_HL_NEG);
          // i_color = RGBA_I_BORDER_HL_NEG;
        }
        else{
          set_color_inc_pred(RGBA_I_NEG);
          // i_color = RGBA_I_BORDER_NEG;
        }

        // height = (bar->y - ypos_scaled); // height should not be gapped imo need test // + zero_gap_scaled
        height = fabsf(bar->height);

        // if(accel.integer & ACCEL_LINE_ACTIVE)
        // {
        //   line_height = (height > line_size_scaled ? line_size_scaled : height);
        //   // if border does not cover whole area, draw rest of the bar
        //   if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
        //   {
        //     // draw uncovered area 
        //     draw_negative_vc(bar->x, bar->y - line_height, bar->width, height - line_height, height);
        //   }
        //   set_color_inc_pred(i_color);
        //   draw_negative_vc(bar->x, bar->y, bar->width, line_height, height);
        // }
        // else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
        // ^ cannot be disabled anymore
        {
          draw_negative(bar->x, bar->y, bar->width, height);
        }
      } // zero bars are separated
    } while(0);

    // vertical line's drawing
    // if(!negative_draw_skip && bar->value && accel.integer & ACCEL_VL_ACTIVE)
    // {
    //   dist_to_zero = fabs(bar->y - ypos_scaled);

    //   // for each side
    //   for(j = -1; j <= 1; j+= 2)
    //   {
    //     bar_adj = j == -1 ? bar->prev : bar->next;
    //     // check for adjecent and polarity
    //     if(!bar_adj || bar->polarity != bar_adj->polarity || bar->order + j != bar_adj->order){
    //       vline_to(bar, j, dist_to_zero);
    //       continue;
    //     }

    //     // we have adjecent bar and same polarity only case we need to calculate with the adjecent

    //     // skip vline for lower bar
    //     if((bar->polarity == 1 && bar->value < bar_adj->value)
    //           || (bar->polarity == -1 && bar->value > bar_adj->value)) continue;

    //     dist_to_adj = fabs(bar_adj->y - bar->y);
    //     vline_to(bar, j, dist_to_adj + (accel_vline.integer & ACCEL_VL_LINE_H ? fmin(dist_to_zero - dist_to_adj, line_size_scaled) : 0));
    //   }
    // } // /vlines
  } // /for each bar

  // window center drawing (this one is specific to window bar so no need to have it in bar loop)
  if(!predict && window_bar){
    if(accel_window_center.integer){
      // would be easier to create actual or just helper bar and just pass it to the is_within_angle function
      if(accel_window_center.integer & ACCEL_WINDOW_CENTER_HL && window_bar->angle - window_parts_size * resolution_ratio_inv * x_angle_ratio > 0 && window_bar->angle - (window_parts_size + window_center_size_calculated) * resolution_ratio_inv * x_angle_ratio < 0){
        set_color_inc_pred(RGBA_I_WINDOW_CENTER_HL);
        // i_color = RGBA_I_BORDER_WINDOW_CENTER_HL;
      }
      else{
        set_color_inc_pred(RGBA_I_WINDOW_CENTER);
        // i_color = RGBA_I_BORDER_WINDOW_CENTER;
      }
      //height = ypos_scaled - window_bar->y;
      height = window_bar->height;

       if(accel_window_center_height.value > 0){
        height = accel_window_center.integer & ACCEL_WINDOW_CENTER_RELATIVE_HEIGHT ? (accel_window_center_height.value / 100) * height : window_center_height_scaled;
      }

      if(accel_window_center_min_height.value > 0 && height < window_center_min_height_scaled){
        height = window_center_min_height_scaled;
      }

      float y_target;
      if(((accel_window_center.integer & ACCEL_WINDOW_CENTER_VCENTER_FORCE) && (accel_window_center.integer & ACCEL_WINDOW_CENTER_VCENTER))
        || (!(accel_window_center.integer & ACCEL_WINDOW_CENTER_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
      ){
        y_target = hud_ypos_scaled - vcenter_offset_scaled - height * half_inv;
      }else{
        y_target = hud_ypos_scaled - height;
      }

      if(accel_window_center_voffset.value > 0){
        y_target -= window_center_voffset_scaled;
      }

      // if(accel.integer & ACCEL_LINE_ACTIVE)
      // {
      //   line_height = (height > line_size_scaled ? line_size_scaled : height); // (height - line_size_scaled)
      //   // if border does not cover whole area, draw rest of the bar
      //   if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
      //   {
      //     // draw uncovered area
      //     if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT){
      //       if(aim_zone_size_calculated > window_parts_size && aim_zone_size_calculated < window_parts_size + window_center_size_calculated){
      //         float cut_size = aim_zone_size_calculated - window_parts_size;
      //         if(aim_zone_on_right_side){
      //           draw_positive_nvc(window_bar->x + window_parts_size, y_target + line_height, window_center_size_calculated - cut_size, height - line_height);
      //         }
      //         else
      //         {
      //           draw_positive_nvc(window_bar->x + window_parts_size + cut_size, y_target + line_height, window_center_size_calculated - cut_size, height - line_height);
      //         }
      //       } else if(aim_zone_size_calculated < window_parts_size){
      //         // no cutting needed
      //         draw_positive_nvc(window_bar->x + window_parts_size, y_target + line_height, window_center_size_calculated, height - line_height);
      //       } // else whole center is cutted off
      //     }
      //     else // no subtraction of aim zone
      //     {
      //       draw_positive_nvc(window_bar->x + window_parts_size, y_target + line_height, window_center_size_calculated, height - line_height);
      //     }
      //   }
      //   // draw border line
      //   set_color_inc_pred(i_color);
      //   if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT){
      //     if(aim_zone_size_calculated > window_parts_size && aim_zone_size_calculated < window_parts_size + window_center_size_calculated){
      //       float cut_size = aim_zone_size_calculated - window_parts_size;
      //       if(aim_zone_on_right_side){
      //         draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated - cut_size, line_height);
      //       }
      //       else
      //       {
      //         draw_positive_nvc(window_bar->x + window_parts_size + cut_size, y_target, window_center_size_calculated - cut_size, line_height);
      //       }
      //     } else if(aim_zone_size_calculated < window_parts_size){
      //       // no cutting needed
      //       draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated, line_height);
      //     } // else whole center is cutted off
      //   }
      //   else // no subtraction of aim zone
      //   {
      //     draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated, line_height);
      //   }
      // }
      // else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
      // ^ cannot be disabled anymore
      {
        if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT){
          if(aim_zone_size_calculated > window_parts_size && aim_zone_size_calculated < window_parts_size + window_center_size_calculated){
            float cut_size = aim_zone_size_calculated - window_parts_size;

            if(aim_zone_on_right_side){
              draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated - cut_size, height);
            }
            else
            {
              draw_positive_nvc(window_bar->x + window_parts_size + cut_size, y_target, window_center_size_calculated - cut_size, height);
            } 
          } else if(aim_zone_size_calculated < window_parts_size){
            // no cutting needed
            draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated, height);
          } // else whole center is cutted off
        }
        else // no subtraction of aim zone
        {
          draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated, height);
        }
      }
    }

    // aim zone
    if(accel_aim_zone.integer){
      float aim_zone_offset = 0;
      // at which side of window bar is located the aim zone
      if(aim_zone_on_right_side){
        // right
        aim_zone_offset = window_bar->width - aim_zone_size_calculated;
      }

      // would be easier to create actual or just helper bar and just pass it to the is_within_angle function
      if(accel_aim_zone.integer & ACCEL_AIM_HL && window_bar->angle - aim_zone_offset * resolution_ratio_inv * x_angle_ratio > 0 && window_bar->angle - (aim_zone_offset + aim_zone_size_calculated) * resolution_ratio_inv * x_angle_ratio < 0){
        set_color_inc_pred(RGBA_I_WINDOW_END_HL);
        // i_color = RGBA_I_BORDER_AIM_ZONE_HL;
      }
      else{
        set_color_inc_pred(RGBA_I_WINDOW_END);
        // i_color = RGBA_I_BORDER_AIM_ZONE;
      }

      const float x = window_bar->x + aim_zone_offset;

      height = window_bar->height;

      if(accel_aim_zone_height.value > 0){
        height = accel_aim_zone.integer & ACCEL_AIM_RELATIVE_HEIGHT ? (accel_aim_zone_height.value / 100) * height : aim_zone_height_scaled;
      }

      if(accel_aim_zone_min_height.value > 0 && height < aim_zone_min_height_scaled){
        height = window_end_min_height_scaled;
      }

      float y_target;
      if(((accel_aim_zone.integer & ACCEL_AIM_VCENTER_FORCE) && (accel_aim_zone.integer & ACCEL_AIM_VCENTER))
        || (!(accel_aim_zone.integer & ACCEL_AIM_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
      ){
        y_target = hud_ypos_scaled - vcenter_offset_scaled - height * half_inv;
      }
      else
      {
        y_target = hud_ypos_scaled - height;
      }

      if(accel_aim_zone_voffset.value > 0){
        y_target -= window_end_voffset_scaled;
      }

      // // if(accel_aim_zone_height.value > 0){
      // //   draw_positive_nvc(x, ypos_scaled - aim_zone_height_scaled - (accel_aim_zone.integer & ACCEL_AIM_VCENTER ? vcenter_offset_scaled - aim_zone_height_scaled / 2 : 0), aim_zone_size_calculated, aim_zone_height_scaled);
      // // }
      // // else
      // if(accel.integer & ACCEL_LINE_ACTIVE)
      // {
      //   line_height = (height > line_size_scaled ? line_size_scaled : height);
      //   // if border does not cover whole area, draw rest of the bar
      //   if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
      //   {
      //     // draw uncovered area
      //     draw_positive_nvc(x, y_target + line_height, aim_zone_size_calculated, height - line_height);
      //   }
      //   // draw border line
      //   set_color_inc_pred(i_color);
      //   draw_positive_nvc(x, y_target, aim_zone_size_calculated, line_height);
      // }
      // else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
      // ^ cannot be disabled anymore
      {
        draw_positive_nvc(x, y_target, aim_zone_size_calculated, height);
      }
    }
  }

    // edges
    if(accel_edge.integer){
      int right_i_color;
      for(it = start; it && it != end->next; it = it->next)
      {
        bar = it;
        if(!(bar->polarity == 1 && (!bar->prev || bar->prev == start->prev || bar->polarity != bar->prev->polarity || bar->order != bar->prev->order + 1))) { continue; }

        bar_tmp = bar;
        // find end of positive window
        while(bar_tmp->next && bar_tmp->next->polarity == 1 && bar_tmp->next != end->next){
          bar_tmp = bar_tmp->next;
        }

        // special handling for single positive bar ?

        // potentially wrong (sign ?)
        if(angle_short_radial_distance(vel_angle, yaw_angle + bar->angle_start) < 0){
          i_color = RGBA_I_EDGE_NEAR;
          right_i_color = RGBA_I_EDGE_FAR;
        }
        else {
          i_color = RGBA_I_EDGE_FAR;
          right_i_color = RGBA_I_EDGE_NEAR;
        }

        // get edges height per case
        float lh = 0, rh = 0;

        if(accel_edge_height.value > 0){
          if(accel_edge.integer & ACCEL_EDGE_RELATIVE_HEIGHT){
            // if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
            {
              lh = bar->height * (accel_edge_height.value / 100);
              rh = bar_tmp->height * (accel_edge_height.value / 100);
            }
            // else if(accel.integer & ACCEL_LINE_ACTIVE)
            // {
            //   lh = (bar->height > line_size_scaled ? line_size_scaled : bar->height) * (accel_edge_height.value / 100);
            //   rh = (bar_tmp->height > line_size_scaled ? line_size_scaled : bar_tmp->height) * (accel_edge_height.value / 100);
            // }
          }
          else
          {
            rh = lh = edge_height_scaled;
          }
        }
        else // if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
        {
          lh = bar->height;
          rh = bar_tmp->height;
        }
        // else if(accel.integer & ACCEL_LINE_ACTIVE)
        // {
        //   lh = bar->height > line_size_scaled ? line_size_scaled : bar->height;
        //   rh = bar_tmp->height > line_size_scaled ? line_size_scaled : bar_tmp->height;
        // }
        // else // no need to draw anything in the rest of the cases
        // {
        //   // skip all positive we just handled
        //   it = bar_tmp;
        //   continue;
        // }

        // apply min height // before FULL_SIZE ? 
        if(accel_edge_min_height.value > 0){
          lh = lh < edge_min_height_scaled ? edge_min_height_scaled : lh;
          rh = rh < edge_min_height_scaled ? edge_min_height_scaled : rh;
        }

        float ly = hud_ypos_scaled - lh,
              ry = hud_ypos_scaled - rh;

        if(accel_edge.integer & ACCEL_EDGE_FULL_SIZE){
          lh = lh * 2 + gap_scaled; 
          rh = rh * 2 + gap_scaled;
        }

        if(((accel_edge.integer & ACCEL_EDGE_VCENTER_FORCE) && (accel_edge.integer & ACCEL_EDGE_VCENTER))
          || (!(accel_edge.integer & ACCEL_EDGE_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
        ){
          ly = hud_ypos_scaled - vcenter_offset_scaled - lh * half_inv;
          ry = hud_ypos_scaled - vcenter_offset_scaled - rh * half_inv;
        }

        float lw = edge_size_scaled,
              rw = edge_size_scaled;

        if(accel_edge.integer & ACCEL_EDGE_RELATIVE_SIZE){
          lw = bar->width * (accel_edge_size.value / 100);
          rw = bar_tmp->width * (accel_edge_size.value / 100);
        }

        if(accel_edge_min_size.value > 0 && lw < edge_min_size_scaled){
          lw = edge_min_size_scaled;
        }

        if(accel_edge_min_size.value > 0 && rw < edge_min_size_scaled){
          rw = edge_min_size_scaled;
        }

        const float lx = bar->x - lw,
                    rx = bar_tmp->x + bar_tmp->width;

        if(accel_edge_voffset.value > 0){
          ly -= edge_voffset_scaled;
          ry -= edge_voffset_scaled;
        }

        // left
        trap_R_SetColor(game.graph_rgba[i_color]);
        draw_positive_nvc(lx, ly, lw, lh);

        // right
        trap_R_SetColor(game.graph_rgba[right_i_color]);
        draw_positive_nvc(rx, ry, rw, rh);

        // skip all positive we just handled
        it = bar_tmp;
      }
    }// /edges

    // point line
    if(accel.integer & ACCEL_PL_ACTIVE && center_bar)
    {
      set_color_inc_pred(RGBA_I_POINT);

      y = hud_ypos_scaled + hud_height_scaled * (center_bar->value / normalizer) * -1;
      if(center_bar->value > 0){
        draw_positive(center - (accel_point_line_size.value * cgs.screenXScale) * half_inv, y, accel_point_line_size.value * cgs.screenXScale, ypos_scaled - y);
      }
      else if(center_bar->value < 0){
        draw_negative(center - (accel_point_line_size.value * cgs.screenXScale) * half_inv, y, accel_point_line_size.value * cgs.screenXScale, y - ypos_scaled);
      }
    } // /point line
  }

  trap_R_SetColor(NULL);
}

static void PM_SlickAccelerate(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  PM_Accelerate(wishdir, wishspeed, accel_, MOVE_WALK_SLICK);
}


/*
===================
PAL_AirMove

===================
*/
static void PM_AirMove( void ) {
  int			i;
  vec3_t		wishvel;
  vec3_t		wishdir;
  float		wishspeed;
  float		scale;


  scale = accel_trueness.integer & ACCEL_TN_JUMPCROUCH || predict == PREDICT_CROUCHJUMP ?
    PM_CmdScale(&a.pm_ps, &a.pm.cmd) :
    PM_AltCmdScale(&a.pm_ps, &a.pm.cmd);

  // project moves down to flat plane
  game.pml.forward[2] = 0;
  game.pml.right[2] = 0;
  VectorNormalize (game.pml.forward);
  VectorNormalize (game.pml.right);

  for ( i = 0 ; i < 2 ; i++ ) {
      wishvel[i] = game.pml.forward[i]*game.pm.cmd.forwardmove + game.pml.right[i]*game.pm.cmd.rightmove;
  }
  wishvel[2] = 0;

  VectorCopy (wishvel, wishdir);
  wishspeed = VectorNormalize(wishdir);
  wishspeed *= scale;

  // not on ground, so little effect on velocity
  if (game.pm_ps.pm_flags & PMF_PROMODE && accel_trueness.integer & ACCEL_TN_CPM) //  && (!pms.pm.cmd.forwardmove && pms.pm.cmd.rightmove) => there is also forward move
  {
    if(!game.pm.cmd.forwardmove && game.pm.cmd.rightmove){
      PM_Accelerate(wishdir,
        (wishspeed > cpm_airwishspeed ? cpm_airwishspeed : wishspeed),
        cpm_airstrafeaccelerate, MOVE_AIR_CPM);
    }
    else {
      PM_Accelerate(wishdir, wishspeed,
        (DotProduct(game.pm_ps.velocity, wishdir) < 0 ? 2.5f : pm_airaccelerate),
      MOVE_AIR_CPM);
    }
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate, MOVE_AIR);
  }
}


/*
===================
PAL_WalkMove

===================
*/
static void PM_WalkMove( void ) {
  int			i;
  vec3_t		wishvel;
  vec3_t		wishdir;
  float		wishspeed;
  float		scale;

  if ( game.pm.waterlevel > 2 && DotProduct( game.pml.forward, game.pml.groundTrace.plane.normal ) > 0 ) {
    // begin swimming
    // PAL_WaterMove();
    return;
  }

  if (PM_CheckJump(&game.pm, &game.pm_ps, &game.pml)) {
    // jumped away
    if ( game.pm.waterlevel > 1 ) {
        //PAL_WaterMove();
    } else {
        PM_AirMove();
    }
    return;
  }

  scale = accel_trueness.integer & ACCEL_TN_JUMPCROUCH || predict == PREDICT_CROUCHJUMP ?
  PM_CmdScale(&a.pm_ps, &a.pm.cmd) :
  PM_AltCmdScale(&a.pm_ps, &a.pm.cmd);

  // project moves down to flat plane
  game.pml.forward[2] = 0;
  game.pml.right[2] = 0;

  // project the forward and right directions onto the ground plane
  PM_ClipVelocity (game.pml.forward, game.pml.groundTrace.plane.normal, game.pml.forward, OVERCLIP );
  PM_ClipVelocity (game.pml.right, game.pml.groundTrace.plane.normal, game.pml.right, OVERCLIP );
  //
  VectorNormalize (game.pml.forward); // aprox. 1 unit in space facing forward based on cameraview
  VectorNormalize (game.pml.right); // aprox. 1 unit in space facing right based on cameraview

  for ( i = 0 ; i < 3 ; i++ ) {
    wishvel[i] = game.pml.forward[i]*game.pm.cmd.forwardmove + game.pml.right[i]*game.pm.cmd.rightmove; // added fractions of direction (the camera once) increased over move (127 run speed)
  }
  // when going up or down slopes the wish velocity should Not be zero
  // wishvel[2] = 0;
  // aka ^ that is intentionally commented out, leaved there to notice the behavior

  VectorCopy (wishvel, wishdir);
  wishspeed = VectorNormalize(wishdir); 
  wishspeed *= scale;

  // clamp the speed lower if ducking
  if ( game.pm_ps.pm_flags & PMF_DUCKED ) {
    if ( wishspeed > game.pm_ps.speed * pm_duckScale ) {
      wishspeed = game.pm_ps.speed * pm_duckScale;
    }
  }

  // clamp the speed lower if wading or walking on the bottom
  if(game.pm.waterlevel)
  {
    float	waterScale = game.pm.waterlevel / 3.0f;
    if(game.pm_ps.pm_flags & PMF_PROMODE)
    {
      waterScale = 1.0 - (1.0 - (game.pm.waterlevel == 1 ? 0.585 : 0.54)) * waterScale;
    }
    else {
      waterScale = 1.0f - ( 1.0f - pm_swimScale ) * waterScale;
    }

    if ( wishspeed > game.pm_ps.speed * waterScale ) {
      wishspeed = game.pm_ps.speed * waterScale;
    }
  }

  // when a player gets hit, they temporarily lose
  // full control, which allows them to be moved a bit
  if (accel_trueness.integer & ACCEL_TN_GROUND)
  {
    if (game.pml.groundTrace.surfaceFlags & SURF_SLICK || game.pm_ps.pm_flags & PMF_TIME_KNOCKBACK)
    {
      PM_SlickAccelerate(wishdir, wishspeed, game.pm_ps.pm_flags & PMF_PROMODE ? cpm_slickaccelerate : pm_slickaccelerate);
    }
    else
    {
      // don't reset the z velocity for slopes
      // a.pm_ps.velocity[2] = 0;
      PM_Accelerate(wishdir, wishspeed, game.pm_ps.pm_flags & PMF_PROMODE ? cpm_accelerate : pm_accelerate, MOVE_WALK);
    }
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate, MOVE_WALK);
  }
}
