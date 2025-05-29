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
#include "cg_cvar.h"
#include "cg_tcvar.h"
#include "cg_local.h"
#include "cg_utils.h"

#include "common.h"
#include "fast_vector_math.h"
#include "q_assert.h"
#include "q_shared.h"

#include "cpu_support.h"
#include "thread_pool.h"

#define ACCEL_DEBUG 0

// **** colors ****

#define COLOR_LIST \
  /* (cvar name, default) */ \
  X(accel_rgba, ".2 .9 .2 .5") \
  X(accel_neg_rgba, ".9 .2 .2 .5") \
  X(accel_near_edge_rgba, ".7 .1 .1 .5") \
  X(accel_far_edge_rgba, ".1 .1 .7 .5") \
  X(accel_hl_rgba, ".3 1 .3 .75") \
  X(accel_hl_neg_rgba, ".9 .3 .3 .75") \
  X(accel_p_strafe_rgba, ".2 .1 .4 .4") \
  X(accel_p_sm_rgba, ".4 .1 .2 .4") \
  X(accel_p_jc_rgba, "1 1 1 1") \
  X(accel_p_jcsm_rgba, "0 0 0 1") \
  X(accel_p_opposite_rgba, ".8 .-8 .8 .3") \
  X(accel_mwindow_end_rgba, ".2 .4 1 .9") \
  X(accel_mwindow_end_hl_rgba, ".3 .6 .9 .9") \
  X(accel_mirror_rgba, ".5 .6 .4 .9")
  // add new colors here 

// color id enum
#define COLOR_ID(n) COLOR_ID_##n
#define X(n,d) COLOR_ID(n),
enum {
  COLOR_LIST
  COLOR_ID_LENGTH
};
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

typedef struct graph_bar_
{
  int   ix;           // x in max pixels (also works as angle)
  int   iwidth;       // width in max pixels (also works as angle)
  float speed_delta;  // selected component of speed_delta
  int   polarity;     // 1 = positive, 0 = zero, -1 = negative
  float angle_width;  // occupied angle within view
  float angle_middle; // occupied angle within view
  float height;       // scaled
  float y;            // scaled
  int   next_is_adj;  // check if we omited or skiped bar

  // bidirectional linked list
  struct graph_bar_ *next;
  struct graph_bar_ *prev;
} graph_bar;

#define GRAPH_MAX_RESOLUTION 8192 // hardcoded, memory block allocation is limitation also
static_assert(GRAPH_MAX_RESOLUTION % 8 == 0, "GRAPH_MAX_RESOLUTION must be evenly divisible by 8");

typedef struct
{
  vec2_t        yh; // y pos, height (not scaled)
  vec4_t        colors[COLOR_ID_LENGTH];

  graph_bar     graph[GRAPH_MAX_RESOLUTION]; // only one graph is plotted at a time
  int           graph_size;           // how much of the ^ array is currently used

  float         speed;
  vec3_t        velocity_s;           // snapped -> solve spectator bug
  float         vel_angle;            // velocity angle
  float         yaw_angle;            // current yaw angle

  int           resolution;           // atm this is read from game setting
  float         resolution_ratio;     // how much real pixels is equal to 1 max pixel
  // float         resolution_ratio_inv; // inverted
  int           resolution_center;    // resolution / 2
  float         x_angle_ratio;        // used to convert x axis size to angle (from max pixels)
                                      // works both ways angle -> x point
  float         to_real_pixel;        // x_angle_ratio * a.resolution_ratio
  float         to_real_pixel_inv;    // 1 / to_real_pixel (to convert back)

  // scaled means: in real pixels
  float         hud_ypos_scaled;
  float         hud_height_scaled;
  float         base_height_scaled;
  float         max_height_scaled;

  float         neg_offset_scaled;
  float         vcenter_offset_scaled;

  float         predict_offset_scaled;
  float         predict_jumpcrouch_offset_scaled;

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
  float         speed_delta_total[GRAPH_MAX_RESOLUTION];
  float         speed_delta_forward[GRAPH_MAX_RESOLUTION];
  float         speed_delta_side[GRAPH_MAX_RESOLUTION];
  float         speed_delta_up[GRAPH_MAX_RESOLUTION];
  float         speed_delta_plane[GRAPH_MAX_RESOLUTION];

} accel_t;

static accel_t a;

typedef struct {
  signed char cmd_forwardmove;
  signed char cmd_rightmove;
  signed char cmd_upmove;
  int         predict;          // prediction enum
  int         mwindow_mode;     // bool
  int         component;        // component enum
  int         vcenter;          // bool
  float       voffset;
  accel_t     *use_old_a;       // mirror specific (to reuse)
} move_data_t;

// **** cvars ****

static vmCvar_t accel;
static vmCvar_t version;
static vmCvar_t accel_trueness;
static vmCvar_t accel_min_speed;
static vmCvar_t accel_base_height;
static vmCvar_t accel_max_height;
static vmCvar_t accel_yh;
static vmCvar_t accel_negative;
static vmCvar_t accel_mirror;

static vmCvar_t accel_p_strafe;
static vmCvar_t accel_p_sm;
static vmCvar_t accel_p_jc;

static vmCvar_t accel_component;
static vmCvar_t accel_mirror_component;
static vmCvar_t accel_p_strafe_component;
static vmCvar_t accel_p_sm_component;
static vmCvar_t accel_p_jc_component;

static vmCvar_t accel_vcenter;
static vmCvar_t accel_edge_vcenter;
static vmCvar_t accel_mwindow_end_vcenter;
static vmCvar_t accel_p_strafe_vcenter;
static vmCvar_t accel_p_sm_vcenter;
static vmCvar_t accel_p_jc_vcenter;

static vmCvar_t accel_mirror_voffset;
static vmCvar_t accel_negative_voffset;
static vmCvar_t accel_vcenter_voffset;
static vmCvar_t accel_p_strafe_voffset;
static vmCvar_t accel_p_sm_voffset;
static vmCvar_t accel_p_jc_voffset;
static vmCvar_t accel_p_opposite_voffset;

static vmCvar_t accel_p_jc_overdraw;

static vmCvar_t accel_show_move;
static vmCvar_t accel_show_move_vq3;

static vmCvar_t accel_merge_threshold; // width threshold not delta !
static vmCvar_t accel_window_grow_threshold;
static vmCvar_t accel_window_grow_limit;

static vmCvar_t accel_edge;
static vmCvar_t accel_edge_size;
static vmCvar_t accel_edge_min_size;
static vmCvar_t accel_edge_height;
static vmCvar_t accel_edge_min_height;
static vmCvar_t accel_edge_voffset;

static vmCvar_t accel_mwindow_end;
static vmCvar_t accel_mwindow_end_size;
static vmCvar_t accel_mwindow_end_min_size;
static vmCvar_t accel_mwindow_end_height;
static vmCvar_t accel_mwindow_end_min_height;
static vmCvar_t accel_mwindow_end_voffset;

#define PREDICTION_BY_MOVE_LIST \
  X(accel_p_strafe_w_sm) \
  X(accel_p_strafe_w_fm) \
  X(accel_p_strafe_w_nk) \
  X(accel_p_strafe_w_strafe) \
  X(accel_p_jc_w_strafe) \
  X(accel_p_jc_w_fm) \
  X(accel_p_strafe_w_sm_vq3) \
  X(accel_p_strafe_w_fm_vq3) \
  X(accel_p_strafe_w_nk_vq3) \
  X(accel_p_strafe_w_strafe_vq3) \
  X(accel_p_jc_w_strafe_vq3) \
  X(accel_p_jcsm_w_strafe_vq3) \
  X(accel_p_jc_w_sm_vq3) \
  X(accel_p_jcstrafe_w_sm_vq3) \
  X(accel_p_sm_w_sm_vq3) \
  X(accel_p_sm_w_strafe_vq3) \
  X(accel_p_sm_w_fm_vq3) \
  X(accel_p_sm_w_nk_vq3)

// accel_p_jc_w_fm_vq3 doesn't make sense, the forward move is not useful there, 
// also there are two option strafe and sidemove, how to decide ?...

// prediction cvars
#define X(n) static vmCvar_t n;
PREDICTION_BY_MOVE_LIST
#undef X

// color cvars
#define X(n,d) static vmCvar_t n;
COLOR_LIST
#undef X

#if ACCEL_DEBUG
  static vmCvar_t accel_verbose;
#endif // ACCEL_DEBUG


// **** cvar tables ****

// helper to keep cvar names based on the variable name
#define CVAR_EXPAND_NAME(n) n, "p_" #n

static cvarTable_t accel_cvars[] = {
  { &CVAR_EXPAND_NAME(accel), "0b0000", CVAR_ARCHIVE_ND },
  // #define ACCEL_DISABLED            0
  #define ACCEL_ENABLE              1 // the basic view
  #define ACCEL_HL_ACTIVE           (1 << 1) // highlight active
  #define ACCEL_UNIFORM_VALUE       (1 << 2) // uniform values
  #define ACCEL_MWINDOW             (1 << 3) // draw only window bar

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
  { &CVAR_EXPAND_NAME(accel_negative), "0b000", CVAR_ARCHIVE_ND },
  //#define ACCEL_NEG_MODE          0 // negative disabled
  #define ACCEL_NEGATIVE_ENABLED    1 // negative enabled
  #define ACCEL_NEGATIVE_ADJECENT   (1 << 1) // only adjecent negative are shown
  #define ACCEL_NEGATIVE_UP         (1 << 2) // negatives grow up (not down as default)
  
  { &CVAR_EXPAND_NAME(accel_mirror), "0", CVAR_ARCHIVE_ND },
  //#define ACCEL_MIRROR_DISABLED     0 // mirror disabled
  #define ACCEL_MIRROR_ENABLED        1 // mirror enabled
  // stacking on top of regular graph won't be implemented (don't believe someone would use it)

  { &CVAR_EXPAND_NAME(accel_p_strafe), "0b000", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_sm), "0b000", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_jc), "0b000", CVAR_ARCHIVE_ND },
  #define ACCEL_PREDICT               1
  #define ACCEL_PREDICT_MWINDOW       (1 << 1) // only main window
  #define ACCEL_PREDICT_UNIFORM_VALUE (1 << 2) // uniform values
  
  { &CVAR_EXPAND_NAME(accel_component), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_mirror_component), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_strafe_component), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_sm_component), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_jc_component), "0", CVAR_ARCHIVE_ND },
  #define ACCEL_COMPONENT_TOTAL   0 // forward + side + up
  #define ACCEL_COMPONENT_PLANE   1 // forward + side
  #define ACCEL_COMPONENT_FORWARD 2
  #define ACCEL_COMPONENT_SIDE    3
  #define ACCEL_COMPONENT_UP      4

  { &CVAR_EXPAND_NAME(accel_vcenter), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_vcenter), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_mwindow_end_vcenter), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_strafe_vcenter), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_sm_vcenter), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_jc_vcenter), "2", CVAR_ARCHIVE_ND },
    // #define ACCEL_VCENTER_DISABLED 0   // disabled no matter what
  #define ACCEL_VCENTER_ENABLED  1        // enabled no matter what
  #define ACCEL_VCENTER_GENERAL  2        // use accel general vcenter setting (accel_vcenter do not have this option)


  { &CVAR_EXPAND_NAME(accel_mirror_voffset), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_negative_voffset), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_vcenter_voffset), "15", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_strafe_voffset), "30", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_sm_voffset), "30", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_jc_voffset), "0", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_p_opposite_voffset), "0", CVAR_ARCHIVE_ND },

  { &CVAR_EXPAND_NAME(accel_p_jc_overdraw), "0", CVAR_ARCHIVE_ND },

  // enable regular accel graph while holding specific keys
  { &CVAR_EXPAND_NAME(accel_show_move), "0b1101", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_show_move_vq3), "0b1111", CVAR_ARCHIVE_ND },
  #define ACCEL_MOVE_STRAFE         1
  #define ACCEL_MOVE_SIDE           (1 << 1)
  #define ACCEL_MOVE_FORWARD        (1 << 2)
  #define ACCEL_MOVE_SIDE_GROUNDED  (1 << 3)

  { &CVAR_EXPAND_NAME(accel_merge_threshold), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_window_grow_threshold), ".0327", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_window_grow_limit), "1", CVAR_ARCHIVE_ND },

  { &CVAR_EXPAND_NAME(accel_edge), "0b0000", CVAR_ARCHIVE_ND },
  //#define ACCEL_EDGE_ENABLE 1
  #define ACCEL_EDGE_FULL_SIZE        (1 << 1) // extend to negative (double size + gap)
  #define ACCEL_EDGE_RELATIVE_SIZE    (1 << 2) // making p_accel_edge_size percentage of bar width
  #define ACCEL_EDGE_RELATIVE_HEIGHT  (1 << 3) // making p_accel_edge_height percentage of bar height

  { &CVAR_EXPAND_NAME(accel_edge_size), "1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_min_size), "2", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_min_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_edge_voffset), "0", CVAR_ARCHIVE_ND },

  { &CVAR_EXPAND_NAME(accel_mwindow_end), "0b00000", CVAR_ARCHIVE_ND },
  #define ACCEL_MWINDOW_END_HL              (1 << 1)   // highlight
  #define ACCEL_MWINDOW_END_RELATIVE_SIZE   (1 << 2)   // making p_accel_mwindow_end_size percentage of bar width
  #define ACCEL_MWINDOW_END_RELATIVE_HEIGHT (1 << 3)   // making p_accel_mwindow_end_height percentage of bar height
  #define ACCEL_MWINDOW_END_SUBTRACT        (1 << 4)  // cutoff the "covered" area of window zone

  { &CVAR_EXPAND_NAME(accel_mwindow_end_size), "10", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_mwindow_end_min_size), "10", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_mwindow_end_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_mwindow_end_min_height), "-1", CVAR_ARCHIVE_ND },
  { &CVAR_EXPAND_NAME(accel_mwindow_end_voffset), "0", CVAR_ARCHIVE_ND },

  #define X(n) { &CVAR_EXPAND_NAME(n), "0", CVAR_ARCHIVE_ND },
  PREDICTION_BY_MOVE_LIST
  #undef X

  #define X(n,d) { &CVAR_EXPAND_NAME(n), d, CVAR_ARCHIVE_ND },
  COLOR_LIST
  #undef X

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
  X(accel_mwindow_end)

// binary parsing or vector parsing is good enough reason to track
#define TRACK_LIST_BINARY \
  X(accel_trueness) \
  X(accel_negative) \
  X(accel_p_strafe) \
  X(accel_p_sm) \
  X(accel_p_jc) \
  X(accel_show_move) \
  X(accel_show_move_vq3)

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

// track id enum
#define TRACK_ID(n) TRACK_ID_##n
enum {
  #define X(n) TRACK_ID(n),
  TRACK_LIST_SPECIAL
  TRACK_LIST_BINARY
  #undef X
  #define X(n,d) TRACK_ID(n),
  TRACK_LIST_VEC2
  TRACK_LIST_VEC4
  #undef X
  TRACK_ID_LENGTH
};

static trackTableItem accel_track_cvars[] = {
  #define X(n) [TRACK_ID(n)] = { &CVAR_EXPAND_NAME(n), 0, TRACK_CALLBACK_NAME(n), 0 },
  TRACK_LIST_SPECIAL
  #undef X
  #define X(n) [TRACK_ID(n)] = { &CVAR_EXPAND_NAME(n), 0, _tcb_binary, 0 },
  TRACK_LIST_BINARY
  #undef X
  #define X(n,t) [TRACK_ID(n)] = { &CVAR_EXPAND_NAME(n), 0, _tcb_vec2, t },
  TRACK_LIST_VEC2
  #undef X
  #define X(n,t) [TRACK_ID(n)] = { &CVAR_EXPAND_NAME(n), 0, _tcb_vec4, t },
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
  PREDICT_JUMPCROUCH,
  PREDICT_JUMPCROUCH_SM,
  PREDICT_MIRROR // mirror is internally threaded like prediction
};

enum {
  MOVE_AIR,
  MOVE_AIR_CPM,
  MOVE_WALK,
  MOVE_WALK_SLICK,
};

// no help here, just because there is no space in the proxymod help table and i don't want to modify it (for now)

// **** init functions ****

// require resolution and fov (x_angle_ratio)
// there is no need to make this super performant since the change of resolution or fov is rare
static void precalc_trig_tables(void)
{
  int   i;
  float angle;
  // for each horizontal pixel
  for(i = 0; i < a.resolution; ++i)
  {
    angle = (i - a.resolution_center) * a.to_real_pixel; // (left < 0, right > 0)
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
  a.half_fov_x = cg.refdef.fov_x * .5f;
  a.half_fov_x_tan_inv = 1.f / tanf(a.half_fov_x);
  a.quarter_fov_x = cg.refdef.fov_x / 4;
  a.quarter_fov_x_tan_inv = 1.f / tanf(a.quarter_fov_x);

  // set guard
  a.last_vid_width = cgs.glconfig.vidWidth;

  // initial
  a.half_screen_width = cgs.glconfig.vidWidth * .5f;

  a.resolution = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ? GRAPH_MAX_RESOLUTION : cgs.glconfig.vidWidth;
  a.resolution_center = a.resolution * .5f;
  a.resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
      cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION
      : 1.f;
  //a.resolution_ratio_inv = 1.f / a.resolution_ratio;

  // use both above
  a.x_angle_ratio = cg.refdef.fov_x / a.resolution;
  a.to_real_pixel = a.x_angle_ratio * a.resolution_ratio;
  a.to_real_pixel_inv = 1.f / a.to_real_pixel;

  // pre calc sin/cos tables
  precalc_trig_tables();

  // set guard
  a.last_screen_x_scale = cgs.screenXScale;

  // initial
  a.hud_ypos_scaled = a.yh[0] * cgs.screenXScale;
  a.hud_height_scaled = a.yh[1] * cgs.screenXScale;

  a.predict_offset_scaled = accel_p_jc_voffset.value * cgs.screenXScale;
  a.predict_jumpcrouch_offset_scaled = accel_p_jc_voffset.value * cgs.screenXScale;

  a.edge_size_scaled = accel_edge_size.value * cgs.screenXScale;
  a.edge_min_size_scaled = accel_edge_min_size.value * cgs.screenXScale;
  a.edge_height_scaled = accel_edge_height.value * cgs.screenXScale;
  a.edge_min_height_scaled = accel_edge_min_height.value * cgs.screenXScale;
  a.edge_voffset_scaled = accel_edge_voffset.value * cgs.screenXScale;

  a.window_end_size_scaled = accel_mwindow_end_size.value * cgs.screenXScale;
  a.window_end_min_size_scaled = accel_mwindow_end_min_size.value * cgs.screenXScale;
  a.window_end_height_scaled = accel_mwindow_end_height.value * cgs.screenXScale;
  a.window_end_min_height_scaled = accel_mwindow_end_min_height.value * cgs.screenXScale;
  a.window_end_voffset_scaled = accel_mwindow_end_voffset.value * cgs.screenXScale;
  a.base_height_scaled = accel_base_height.value * cgs.screenXScale;
  a.max_height_scaled = accel_max_height.value * cgs.screenXScale;
  a.neg_offset_scaled = accel_negative_voffset.value * cgs.screenXScale;
  a.vcenter_offset_scaled = accel_vcenter_voffset.value * cgs.screenXScale;
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
    a.half_fov_x = cg.refdef.fov_x * .5f;
    a.half_fov_x_tan_inv = 1.f / tanf(a.half_fov_x);
    a.quarter_fov_x = cg.refdef.fov_x / 4;
    a.quarter_fov_x_tan_inv = 1.f / tanf(a.quarter_fov_x);
  }

  // vid_width guard
  if(a.last_vid_width != cgs.glconfig.vidWidth)
  {
    a.last_vid_width = cgs.glconfig.vidWidth;
    a.half_screen_width = cgs.glconfig.vidWidth * .5f;

    a.resolution = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ? GRAPH_MAX_RESOLUTION : cgs.glconfig.vidWidth;
    a.resolution_center = a.resolution * .5f;
    a.resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
        cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION
        : 1.f;
    //a.resolution_ratio_inv = 1.f / a.resolution_ratio;
  }

  if(fov_or_width_change)
  {
    a.x_angle_ratio = cg.refdef.fov_x / a.resolution;
    a.to_real_pixel = a.x_angle_ratio * a.resolution_ratio;
    a.to_real_pixel_inv = 1.f / a.to_real_pixel;
    precalc_trig_tables();
  }

  // x_scale guard
  if(a.last_screen_x_scale != cgs.screenXScale)
  {
    a.last_screen_x_scale = cgs.screenXScale;

    a.hud_ypos_scaled = a.yh[0] * cgs.screenXScale;
    a.hud_height_scaled = a.yh[1] * cgs.screenXScale;

    a.predict_offset_scaled = accel_p_jc_voffset.value * cgs.screenXScale;
    a.predict_jumpcrouch_offset_scaled = accel_p_jc_voffset.value * cgs.screenXScale;

    a.edge_size_scaled = accel_edge_size.value * cgs.screenXScale;
    a.edge_min_size_scaled = accel_edge_min_size.value * cgs.screenXScale;
    a.edge_height_scaled = accel_edge_height.value * cgs.screenXScale;
    a.edge_min_height_scaled = accel_edge_min_height.value * cgs.screenXScale;
    a.edge_voffset_scaled = accel_edge_voffset.value * cgs.screenXScale;

    a.window_end_size_scaled = accel_mwindow_end_size.value * cgs.screenXScale;
    a.window_end_min_size_scaled = accel_mwindow_end_min_size.value * cgs.screenXScale;
    a.window_end_height_scaled = accel_mwindow_end_height.value * cgs.screenXScale;
    a.window_end_min_height_scaled = accel_mwindow_end_min_height.value * cgs.screenXScale;
    a.window_end_voffset_scaled = accel_mwindow_end_voffset.value * cgs.screenXScale;
    a.base_height_scaled = accel_base_height.value * cgs.screenXScale;
    a.max_height_scaled = accel_max_height.value * cgs.screenXScale;
    a.neg_offset_scaled = accel_negative_voffset.value * cgs.screenXScale;
    a.vcenter_offset_scaled = accel_vcenter_voffset.value * cgs.screenXScale;
  }
}


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
  *y -= a.vcenter_offset_scaled - h * .5f; // assume h is always positive (might need abs here)
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
  proj_x = a.half_screen_width * (1 + tanf(angle * .5f) * a.quarter_fov_x_tan_inv);
  angle = ((*x + *w) / a.half_screen_width - 1) * a.half_fov_x;
  proj_w = (a.half_screen_width * (1 + tanf(angle * .5f) * a.quarter_fov_x_tan_inv)) - proj_x;

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

  void (*tmp)(float*, float);
  tmp = vertical_center;

  // update all related function pointers
  if(item->vmCvar->integer & ACCEL_VCENTER){
    // enable vertical centering
    vertical_center = _vertical_center;
  } else {
    // disable vertical centering
    vertical_center = _vertical_center_noop;
  }

  // these itself can be used for tracking changes
  if(tmp != vertical_center){
    TRACK_CALLBACK_NAME(accel_mwindow_end)(&accel_track_cvars[TRACK_ID(accel_mwindow_end)], NULL);
    TRACK_CALLBACK_NAME(accel_edge)(&accel_track_cvars[TRACK_ID(accel_edge)], NULL);
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
  if(((item->vmCvar->integer & ACCEL_EDGE_VCENTER_FORCE) && (item->vmCvar->integer & ACCEL_EDGE_VCENTER))
    || (!(item->vmCvar->integer & ACCEL_EDGE_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
  ){
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
  if(((item->vmCvar->integer & ACCEL_MWINDOW_END_VCENTER_FORCE) && (item->vmCvar->integer & ACCEL_MWINDOW_END_VCENTER))
    || (!(item->vmCvar->integer & ACCEL_MWINDOW_END_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
  ){
    draw_positive_window_end = draw_positive;
  }
  else
  {
    draw_positive_window_end = _draw_positive_nvc;
  }
}

inline static void PmoveSingle(void);
inline static void PmoveSingle_update(void);
inline static void PM_WalkMove(const move_data_t *move_data);
inline static void PM_AirMove(const move_data_t *move_data);

void (*calc_speed_delta_walk_worker)(int, int, void*);
void (*calc_speed_delta_air_vq3_worker)(int, int, void*);
void (*calc_speed_delta_air_cpm_worker)(int, int, void*);
void (*set_speed_delta_job_data)(
  void*, int, float*, float*, vec3_t, float, float, vec3_t, vec3_t,
  int, int, int, vec3_t, float*, float*, float*, float*, float*
);
void *speed_delta_job_data;

union {
  #include "simd_sse2.def.h"
  SIMD_SUFFIX(speed_delta_job_t) SIMD_SUFFIX(speed_delta_job_data);
  #include "simd.undef.h"
  #include "simd_sse41.def.h"
  SIMD_SUFFIX(speed_delta_job_t) SIMD_SUFFIX(speed_delta_job_data);
  #include "simd.undef.h"
  #include "simd_avx.def.h"
  SIMD_SUFFIX(speed_delta_job_t) SIMD_SUFFIX(speed_delta_job_data);
  #include "simd.undef.h"
} speed_delta_job_data_u;

// **** primary hud functions ****
// following functions (init_accel, update_accel, draw_accel, del_accel)
// are entry points, from these everything else is called

void init_accel(void)
{
  init_cvars(accel_cvars, ARRAY_LEN(accel_cvars));
  init_tcvars(accel_track_cvars, ARRAY_LEN(accel_track_cvars));

  // a struct initialization
  a_init();

  // select instruction set
  cpu_support_info_t cpu_info = get_cpu_support();
  if(cpu_info.avx)
  {
    #include "simd_avx.def.h"
    calc_speed_delta_walk_worker = SIMD_SUFFIX(calc_speed_delta_walk_worker);
    calc_speed_delta_air_vq3_worker = SIMD_SUFFIX(calc_speed_delta_air_vq3_worker);
    calc_speed_delta_air_cpm_worker = SIMD_SUFFIX(calc_speed_delta_air_cpm_worker);
    set_speed_delta_job_data = SIMD_SUFFIX(set_speed_delta_job_data);
    speed_delta_job_data = &speed_delta_job_data_u.SIMD_SUFFIX(speed_delta_job_data);
    #include "simd.undef.h"
  }
  else if(cpu_info.sse41)
  {
    #include "simd_sse41.def.h"
    calc_speed_delta_walk_worker = SIMD_SUFFIX(calc_speed_delta_walk_worker);
    calc_speed_delta_air_vq3_worker = SIMD_SUFFIX(calc_speed_delta_air_vq3_worker);
    calc_speed_delta_air_cpm_worker = SIMD_SUFFIX(calc_speed_delta_air_cpm_worker);
    set_speed_delta_job_data = SIMD_SUFFIX(set_speed_delta_job_data);
    speed_delta_job_data = &speed_delta_job_data_u.SIMD_SUFFIX(speed_delta_job_data);
    #include "simd.undef.h"
  }
  else if(cpu_info.sse2)
  {
    #include "simd_sse2.def.h"
    calc_speed_delta_walk_worker = SIMD_SUFFIX(calc_speed_delta_walk_worker);
    calc_speed_delta_air_vq3_worker = SIMD_SUFFIX(calc_speed_delta_air_vq3_worker);
    calc_speed_delta_air_cpm_worker = SIMD_SUFFIX(calc_speed_delta_air_cpm_worker);
    set_speed_delta_job_data = SIMD_SUFFIX(set_speed_delta_job_data);
    speed_delta_job_data = &speed_delta_job_data_u.SIMD_SUFFIX(speed_delta_job_data);
    #include "simd.undef.h"
  }
  else {
    // there is no fallback atm (sse2 is around for 20y)
    ASSERT_TRUE(0);
  }

  // thread pool initialization
  // intend to use SIMD, that is why physical and not logical
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

inline static void move(const move_data_t *move_data)
{
  if (game.pml.walking)
  {
    // walking on ground
    PM_WalkMove(move_data);
  }
  else
  {
    // airborne
    PM_AirMove(move_data);
  }
}

inline static void PmoveSingle_update(void)
{
  // scale is full move, the cmd moves could be partials,
  // particals cause flickering -> p_flickfree force full moves
  // by hooking trap_GetUserCmd
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

  VectorCopy(game.pm_ps.velocity, a.velocity_s);
  Sys_SnapVector(a.velocity_s); // solves bug in spectator mode
}

inline static void PmoveSingle(void)
{
  if(a.draw_block){
    return;
  }

  // drawing gonna happen

  // ***** predictions *****

  // if we have partial move, adjust scale
  const signed char abs_forwardmove = abs(game.pm.cmd.forwardmove);
  const signed char abs_sidemove = abs(game.pm.cmd.rightmove);
  const signed char abs_upmove = abs(game.pm.cmd.upmove);
  // max of these ^
  // we are mimicking the prediction key at the same time as max key was pressed
  signed char related_scale = abs_forwardmove > abs_sidemove ?
    (abs_upmove > abs_forwardmove ? abs_upmove : abs_forwardmove)
    : (abs_upmove > abs_sidemove ? abs_upmove : abs_sidemove);

  if(!related_scale){
    // in case it was partial we don't know how much
    // because there is no move
    related_scale = game.move_scale;
  }
  // in case of full moves the game.move_scale should be the same
  // as game.pm.cmd.forwardmove or game.pm.cmd.rightmove

  const int orig_move_is_strafe = game.pm.cmd.forwardmove && game.pm.cmd.rightmove;
  const int orig_move_is_sm = !game.pm.cmd.forwardmove && game.pm.cmd.rightmove;
  const int orig_move_is_fm = game.pm.cmd.forwardmove && !game.pm.cmd.rightmove;
  const int orig_move_is_nk = !game.pm.cmd.forwardmove && !game.pm.cmd.rightmove;

  if(accel_p_strafe.integer)
  {
    // pre-set
    move_data_t strafe_move_data = {0};
    strafe_move_data.cmd_upmove = game.pm.cmd.upmove; // same
    strafe_move_data.mwindow_mode = accel_p_strafe.integer & ACCEL_PREDICT_MWINDOW;
    strafe_move_data.component = accel_p_strafe_component.integer;
    strafe_move_data.vcenter = accel_p_strafe_vcenter.integer == ACCEL_VCENTER_GENERAL ?
      accel_vcenter.integer : accel_p_strafe_vcenter.integer;
    strafe_move_data.voffset = accel_p_strafe_voffset.value;

    // cpm
    if(game.pm_ps.pm_flags & PMF_PROMODE)
    {
      if(accel_p_strafe_w_sm.integer && orig_move_is_sm){
        // strafe predict
        strafe_move_data.cmd_forwardmove = related_scale;
        strafe_move_data.cmd_rightmove = game.pm.cmd.rightmove;
        strafe_move_data.predict = PREDICT_SM_STRAFE;
        move(&strafe_move_data);
        // opposite side
        strafe_move_data.cmd_rightmove *= -1;
        move(&strafe_move_data);
      }

      if(
        (accel_p_strafe_w_fm.integer & ACCEL_PREDICT && orig_move_is_fm)
        || (accel_p_strafe_w_nk.integer && orig_move_is_nk)
      ){
        // strafe predict
        strafe_move_data.cmd_forwardmove = related_scale;
        strafe_move_data.cmd_rightmove = related_scale;
        strafe_move_data.predict = PREDICT_FMNK_STRAFE;
        move(&strafe_move_data);
        // opposite side
        strafe_move_data.cmd_rightmove *= -1;
        move(&strafe_move_data);
      }

      // predict same move just opposite side
      if(accel_p_strafe_w_strafe.integer && orig_move_is_strafe)
      {
        // predict strafe
        strafe_move_data.cmd_forwardmove = game.pm.cmd.forwardmove;
        strafe_move_data.cmd_rightmove = game.pm.cmd.rightmove * -1;
        strafe_move_data.predict = PREDICT_OPPOSITE;
        move(&strafe_move_data);
      }
    }
    else // vq3
    {
      if(accel_p_strafe_w_sm_vq3.integer && orig_move_is_sm){
        // strafe predict
        strafe_move_data.cmd_forwardmove = related_scale;
        strafe_move_data.cmd_rightmove = game.pm.cmd.rightmove;
        strafe_move_data.predict = PREDICT_SM_STRAFE_ADD;
        move(&strafe_move_data);
        // opposite side
        strafe_move_data.cmd_rightmove *= -1;
        move(&strafe_move_data);
      }

      if(
        (accel_p_strafe_w_fm_vq3.integer & ACCEL_PREDICT && orig_move_is_fm)
        || (accel_p_strafe_w_nk_vq3.integer && orig_move_is_nk)
      ){
        // strafe predict
        strafe_move_data.cmd_forwardmove = related_scale;
        strafe_move_data.cmd_rightmove = related_scale;
        strafe_move_data.predict = PREDICT_FMNK_STRAFE;
        move(&strafe_move_data);
        // opposite side
        strafe_move_data.cmd_rightmove *= -1;
        move(&strafe_move_data);
      }

      // predict same move just opposite side
      if(accel_p_strafe_w_strafe_vq3.integer && orig_move_is_strafe)
      {
        // strafe predict
        strafe_move_data.cmd_forwardmove = game.pm.cmd.forwardmove;
        strafe_move_data.cmd_rightmove = game.pm.cmd.rightmove * -1;
        strafe_move_data.predict = PREDICT_OPPOSITE;
        move(&strafe_move_data);
      }
    }
  }

  if(accel_p_sm.integer && !(game.pm_ps.pm_flags & PMF_PROMODE))
  {
    move_data_t sm_move_data = {0};
    sm_move_data.cmd_upmove = game.pm.cmd.upmove; // same
    sm_move_data.mwindow_mode = accel_p_sm.integer & ACCEL_PREDICT_MWINDOW;
    sm_move_data.component = accel_p_sm_component.integer;
    sm_move_data.vcenter = accel_p_sm_vcenter.integer == ACCEL_VCENTER_GENERAL ?
      accel_vcenter.integer : accel_p_sm_vcenter.integer;
    sm_move_data.voffset = accel_p_sm_voffset.value;

    if(
      (accel_p_sm_w_fm_vq3.integer & ACCEL_PREDICT && orig_move_is_fm)
      || (accel_p_sm_w_nk_vq3.integer && orig_move_is_nk)
    ){
      // sidemove predict
      sm_move_data.cmd_forwardmove = 0;
      sm_move_data.cmd_rightmove = related_scale;
      sm_move_data.predict = PREDICT_FMNK_SM;
      move(&sm_move_data);
      // opposite side
      sm_move_data.cmd_rightmove *= -1;
      move(&sm_move_data);
    }

    // predict same move just opposite side
    if(accel_p_sm_w_sm_vq3.integer && orig_move_is_sm)
    {
      // sidemove predict
      sm_move_data.cmd_forwardmove = 0;
      sm_move_data.cmd_rightmove = game.pm.cmd.rightmove * -1;
      sm_move_data.predict = PREDICT_OPPOSITE;
      move(&sm_move_data);
    }

    if(accel_p_sm_w_strafe_vq3.integer && orig_move_is_strafe){
      // sidemove predict
      sm_move_data.cmd_forwardmove = 0;
      sm_move_data.cmd_rightmove = game.pm.cmd.rightmove;
      sm_move_data.predict = PREDICT_STRAFE_SM;
      move(&sm_move_data);
      // opposite side
      sm_move_data.cmd_rightmove *= -1;
      move(&sm_move_data);
    }
  }

  // crouchjump
  if(!accel_p_jc_overdraw.value){ // this check is flow guard, will be skipped from bottom
    LABEL_JC_OVERDRAW:

    if(accel_p_jc.integer)
    {
      move_data_t jc_move_data = {0};
      jc_move_data.cmd_upmove = related_scale; // same
      jc_move_data.mwindow_mode = accel_p_jc.integer & ACCEL_PREDICT_MWINDOW;
      jc_move_data.component = accel_p_jc_component.integer;
      jc_move_data.vcenter = accel_p_jc_vcenter.integer == ACCEL_VCENTER_GENERAL ?
        accel_vcenter.integer : accel_p_jc_vcenter.integer;
      jc_move_data.voffset = accel_p_jc_voffset.value;

      // cpm
      if(game.pm_ps.pm_flags & PMF_PROMODE)
      {
        // predict jump / crouch strafe while forwardmove
        if(accel_p_jc_w_fm.integer && orig_move_is_fm)
        {
          // jc strafe
          jc_move_data.cmd_forwardmove = game.pm.cmd.forwardmove;
          jc_move_data.cmd_rightmove = related_scale;
          jc_move_data.predict = PREDICT_JUMPCROUCH;
          move(&jc_move_data);
          // opposite side
          jc_move_data.cmd_rightmove *= -1;
          move(&jc_move_data);
        }
      }
      else // vq3
      {
        // predict jump / crouch sidemove while sidemove (same)
        if(accel_p_jc_w_sm_vq3.integer && orig_move_is_sm)
        {
          // jc sidemove
          jc_move_data.cmd_forwardmove = 0;
          jc_move_data.cmd_rightmove = game.pm.cmd.rightmove;
          jc_move_data.predict = PREDICT_JUMPCROUCH_SM;
          move(&jc_move_data);
        }

        // predict jump / crouch sidemove while strafe
        if(accel_p_jcsm_w_strafe_vq3.integer && orig_move_is_strafe)
        {
          // jc sidemove
          jc_move_data.cmd_forwardmove = 0;
          jc_move_data.cmd_rightmove = game.pm.cmd.rightmove;
          jc_move_data.predict = PREDICT_JUMPCROUCH_SM;
          move(&jc_move_data);
        }

        // predict jump / crouch strafe while sidemove
        if(accel_p_jcstrafe_w_sm_vq3.integer && orig_move_is_sm)
        {
          // jc strafe
          jc_move_data.cmd_forwardmove = related_scale;
          jc_move_data.cmd_rightmove = game.pm.cmd.rightmove;
          jc_move_data.predict = PREDICT_JUMPCROUCH;
          move(&jc_move_data);
        }
      }

      // same for both physics
      // predict jump / crouch strafe while strafe (same)
      if((accel_p_jc_w_strafe.integer || accel_p_jc_w_strafe_vq3.integer)
        && orig_move_is_strafe
      ){
        // js strafe
        jc_move_data.cmd_forwardmove = game.pm.cmd.forwardmove;
        jc_move_data.cmd_rightmove = game.pm.cmd.rightmove;
        jc_move_data.predict = PREDICT_JUMPCROUCH;
        move(&jc_move_data);
      }
    }

    if(accel_p_jc_overdraw.value){
      // in case we get here by goto overdraw
      return;
    }
  }

  // check if regular move is disabled
  if((orig_move_is_strafe // strafe
        && (
          (game.pm_ps.pm_flags & PMF_PROMODE && !(accel_show_move.integer & ACCEL_MOVE_STRAFE))
          || (!(game.pm_ps.pm_flags & PMF_PROMODE) && !(accel_show_move_vq3.integer & ACCEL_MOVE_STRAFE))
        ))
      || (orig_move_is_sm // sidemove
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
      || (orig_move_is_fm // forwardmove
        && (
          (game.pm_ps.pm_flags & PMF_PROMODE && !(accel_show_move.integer & ACCEL_MOVE_FORWARD))
          || (!(game.pm_ps.pm_flags & PMF_PROMODE) && !(accel_show_move_vq3.integer & ACCEL_MOVE_FORWARD))
        ))
  ){
    if(accel_p_jc_overdraw.value){
      goto LABEL_JC_OVERDRAW;
    }
    return; // -> regular move is disabled
  }

  // no need they were not overwrited
  // // restore original keys
  // game.pm.cmd.forwardmove = key_forwardmove;
  // game.pm.cmd.rightmove   = key_rightmove;
  // game.pm.cmd.upmove      = key_upmove;

  move_data_t regular_move_data = {0};
  regular_move_data.cmd_forwardmove = game.pm.cmd.forwardmove;
  regular_move_data.cmd_rightmove = game.pm.cmd.rightmove;
  regular_move_data.cmd_upmove = game.pm.cmd.upmove;
  regular_move_data.mwindow_mode = accel.integer & ACCEL_MWINDOW;
  regular_move_data.component = accel_component.integer;
  regular_move_data.vcenter = accel_vcenter.integer;
  regular_move_data.voffset = 0;

  // regular move
  move(&regular_move_data);
 
  if(accel_p_jc_overdraw.value){
    goto LABEL_JC_OVERDRAW;
  }
}

// set drawing color for predictions
inline static void set_color_pred(int predict)
{
  switch(predict){
    case PREDICT_OPPOSITE:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_opposite_rgba)]);
      break;
    }
    case PREDICT_SM_STRAFE:
    case PREDICT_FMNK_STRAFE:
    case PREDICT_SM_STRAFE_ADD:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_strafe_rgba)]);
      break;
    }
    case PREDICT_FMNK_SM:
    case PREDICT_STRAFE_SM:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_sm_rgba)]);
      break;
    }
    case PREDICT_JUMPCROUCH:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_jc_rgba)]);
      break;
    }
    case PREDICT_JUMPCROUCH_SM:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_p_jcsm_rgba)]);
      break;
    }
    case PREDICT_MIRROR:{
      trap_R_SetColor(a.colors[COLOR_ID(accel_mirror_rgba)]);
      break;
    }
    case 0:
    default:{
      // we are not predicting atm
      ASSERT_TRUE(0);
    }
  }
}

// set drawing color (convenience only)
inline static void set_color(int id)
{
  trap_R_SetColor(a.colors[id]);
}


// automatic vertical centering
// do not use for prediction
inline static void draw_positive(float x, float y, float w, float h, float _)
{
  add_projection_x(&x, &w);

  vertical_center(&y, h);

  trap_R_DrawStretchPic(x, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// automatic vertical centering
// ment to be used for predictions -> because of offset
inline static void draw_positive_o(float x, float y, float w, float h, float offset)
{
  add_projection_x(&x, &w);

  vertical_center(&y, h);

  trap_R_DrawStretchPic(x, y + offset, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// no vertical centering
// used for fetures with its own vcenter settings (edges, window_end)
// should be called indirectly like draw_positive_edge, draw_positive_window_end
inline static void _draw_positive_nvc(float x, float y, float w, float h, float _)
{
  add_projection_x(&x, &w);

  trap_R_DrawStretchPic(x, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// no vertical centering
// used for fetures with its own vcenter settings (edges, window_end)
inline static void draw_positive_o_nvc(float x, float y, float w, float h, float offset)
{
  add_projection_x(&x, &w);

  // if(predict){
  //   y_target -= predict == PREDICT_JUMPCROUCH ? predict_jumpcrouch_offset_scaled : predict_offset_scaled;
  // }

  trap_R_DrawStretchPic(x, y + offset, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

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
      y_target -= a.vcenter_offset_scaled - h * .5f;
    }
    else
    {
      y_target += a.vcenter_offset_scaled - h * .5f;
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

/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
inline static void PM_Accelerate(vec3_t wishdir, float const wishspeed, float const accel_, int move_type, int predict, int predict_window) // TODO: remove move_type and predict -> split this function into versions
{
  int i, i_color, walk, sidemove, component, opposite_facing;
  float height, vel_distance_angle, normalizer, normalizer_inv, yaw_min_distance, yaw_max_distance, yaw_distance, norm_speed;
  float *speed_delta_component;
  vec3_t velocity;
  graph_bar *bar, *bar_tmp, *main_window_bar, *center_bar;
  graph_bar *it, *start;
  graph_bar *end; // end is included in loop (last valid element)
  int omit;

  main_window_bar = NULL;
  center_bar = NULL;
  omit = 0;

  walk = move_type == MOVE_WALK || move_type == MOVE_WALK_SLICK;
  sidemove = !game.pm.cmd.forwardmove && game.pm.cmd.rightmove;
  
  // theoretical maximum is: addspeed * sin(45) * 2, but in practice its way less
  // hardcoded for now
   norm_speed = a.speed;
  // dynamic normalizer breaks at 7000ups (value goes negative at this point), since this is an approximation, we can't make it bullet proof, hence this hotfix:
  if( norm_speed > 6000){
     norm_speed = 6000;
  }
  // there is side effect of the approximation and that is changing height profile based on speed,
  // which falsefully give impression that accel is actually higher while it isn't
  // the trueness static boost for those who want real (accurate) height
   normalizer = (accel_trueness.integer & ACCEL_TN_STATIC_BOOST ? (2.56f * 1.41421356237f) : -0.00025f * norm_speed + 1.75f); // inacurate approximation
  if(walk){
     normalizer *= 15.f;
  }
  normalizer_inv = 1 / normalizer;

  // calc speed delta
  VectorCopy(a.velocity_s, velocity);
  PM_Friction(velocity);

  set_speed_delta_job_data(
    speed_delta_job_data,
    a.resolution,
    a.sin_table,
    a.cos_table,
    wishdir,
    wishspeed,
    accel_,
    velocity,
    game.pml.groundTrace.plane.normal,
    wishspeed && sidemove,
    game.pml.groundPlane,
    sidemove,
    game.pm_ps.velocity,
    a.speed_delta_total,
    a.speed_delta_forward,
    a.speed_delta_side,
    a.speed_delta_up,
    a.speed_delta_plane
  );

  if(walk)
  {
    thread_pool_run(&a.thread_pool, calc_speed_delta_walk_worker, speed_delta_job_data);
  }
  else if(move_type == MOVE_AIR_CPM)
  {
    thread_pool_run(&a.thread_pool, calc_speed_delta_air_cpm_worker, speed_delta_job_data);
  }
  else {
    thread_pool_run(&a.thread_pool, calc_speed_delta_air_vq3_worker, speed_delta_job_data);
  }

  // pick speed delta component
  switch(predict){
    case PREDICT_JUMPCROUCH:
    case PREDICT_JUMPCROUCH_SM:
      component = accel_p_jc_component.integer;
      break;
    case PREDICT_FMNK_STRAFE:
    case PREDICT_SM_STRAFE:
    case PREDICT_SM_STRAFE_ADD:
      component = accel_p_strafe_component.integer;
      break;
    case PREDICT_OPPOSITE:
      if(game.pm.cmd.forwardmove){
        component = accel_p_strafe_component.integer;
      }else{
        component = accel_p_sm_component.integer;
      }
      break;
    case PREDICT_FMNK_SM:
    case PREDICT_STRAFE_SM:
      component = accel_p_sm_component.integer;
    case PREDICT_MIRROR:
      component = accel_mirror_component.integer;
    default:
      component = accel_component.integer;
      break;
  }

  switch(component){
    case ACCEL_COMPONENT_PLANE:
      speed_delta_component = &a.speed_delta_plane[0];
        break;
    case ACCEL_COMPONENT_FORWARD:
      speed_delta_component = &a.speed_delta_forward[0];
        break;
    case ACCEL_COMPONENT_SIDE:
      speed_delta_component = &a.speed_delta_side[0];
        break;
    case ACCEL_COMPONENT_UP:
      speed_delta_component = &a.speed_delta_up[0];
        break;
    default:
      speed_delta_component = &a.speed_delta_total[0];
      break;
  }

  // create raw graph bars (they are modified further)
  a.graph_size = 0;

  // for each horizontal pixel 
  for(i = 0; i < a.resolution; ++i)
  {
    if(
      // automatically omit negative accel when plotting predictions
      // also when negatives are disabled ofc
      speed_delta_component[i] <= 0
      && (predict || accel_negative.value == 0)
    ){
      omit = 1;
      continue;
    }

    // grow the previous bar width when speed_delta is the same
    if(
        __builtin_expect(a.graph_size && speed_delta_component[i-1] == speed_delta_component[i], 1)
        && !omit 
    ){
      a.graph[a.graph_size-1].iwidth += 1;
    }
    else{
      bar = &(a.graph[a.graph_size]);
      bar->ix = i; // * a.resolution_ratio;
      bar->polarity = (speed_delta_component[i] > 0) - (speed_delta_component[i] < 0);
      bar->iwidth = 1; // a.resolution_ratio;
      bar->speed_delta = speed_delta_component[i];
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
        use_prev = (
          fabsf(speed_delta_component[bar->ix] - speed_delta_component[bar->prev->ix])
          < fabsf(speed_delta_component[bar->ix] - speed_delta_component[bar->next->ix])
        );
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
  // ! from now on do not use index loop, iterate instead !

  // after merge bar setup
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;
    bar->angle_width = bar->iwidth * a.x_angle_ratio;
    bar->angle_middle = (bar->ix - a.resolution_center) * a.x_angle_ratio + bar->angle_width * .5f;
  }

  // determine the window bar (window bar is build-in now)

  // DotProduct2D(velocity, camera) < 0
  opposite_facing = cosf(a.yaw_angle) * a.velocity_s[0] + sinf(a.yaw_angle) * a.velocity_s[1] < 0;
  // ^ true if velocity is behind camera
  
  yaw_min_distance = 2 * M_PI; // MAX
  yaw_max_distance = 0;
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;
    vel_distance_angle = angle_short_radial_distance(a.vel_angle, a.yaw_angle + bar->angle_middle);

    if(bar->polarity != 1){ continue; }

    yaw_distance = fabsf(vel_distance_angle);
    if(opposite_facing){
      if(yaw_max_distance < yaw_distance){
        yaw_max_distance = yaw_distance;
        main_window_bar = bar;
      }
    }
    else {
      if(yaw_min_distance > yaw_distance){
        yaw_min_distance = yaw_distance;
        main_window_bar = bar;
      }
    }
  }

  // determine the center bar
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;
    if(
      bar->ix <= a.resolution_center
      && bar->ix + bar->iwidth > a.resolution_center // "=" -> next bar
    ){
      center_bar = bar;
      break;
    }
  }
  
  #if ACCEL_DEBUG
  if(accel_verbose.value && center_bar){
    trap_Print(vaf("center_bar->height: %.3f, center_bar->value: %.3f\n", center_bar->height, center_bar->value));
  }
  #endif

  // when drawing just window bar skip all other positive, except when window_grow_threshold is reached
  if(main_window_bar && ((!predict && accel.integer & ACCEL_MWINDOW) || (predict && predict_window)))
  {    
    // grow positive both sides 
    start = main_window_bar;
    for(i = 0; i < accel_window_grow_limit.integer; ++i){
      if(!start->prev || start->angle_width > accel_window_grow_threshold.value || start->prev->polarity != 1 || !start->prev_is_adj || !start->prev->next_is_adj){
        break;
      }
      start = start->prev; // grow
    }
    end = main_window_bar;
    for(i = 0; i < accel_window_grow_limit.integer; ++i){
      if(!end->next || end->angle_width > accel_window_grow_threshold.value || end->next->polarity != 1 || !end->next_is_adj || !end->next->prev_is_adj){
        break;
      }
      end = end->next; // grow
    }

    // grow negatives on both sides (from whenever we ended)
    while(start->prev){ // no limit here
      if(!start->prev || start->prev->polarity != -1 || !start->prev_is_adj || !start->prev->next_is_adj){  // neutral will stop loop that is what we want
        break;
      }
      start = start->prev; // grow
    }
    while(end->next){ // no limit here
      if(!end->next || end->next->polarity != -1 || !end->next_is_adj || !end->next->prev_is_adj){  // neutral will stop loop that is what we want
        break;
      }
      end = end->next; // grow
    }
  }

  // start and end are at final stage
  // all the bars from start to end gonna be drawn
  // -> additional bar setup
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;

    bar->height = a.hud_height_scaled * (accel.integer & ACCEL_UNIFORM_VALUE ? bar->polarity : speed_delta_component[bar->ix] * normalizer_inv);
    if(accel_base_height.value > 0){
      bar->height += a.base_height_scaled * (bar->height > 0 ? 1 : -1); 
    }
    if(fabsf(bar->height) > a.max_height_scaled){
      bar->height = a.max_height_scaled * (bar->height > 0 ? 1 : -1);
    }

    bar->y = a.hud_ypos_scaled - bar->height; // - because of y axis orientation
  }

  // calculate actual size of window_end
  float window_end_size_calculated = 0;
  int window_end_on_right_side = 0;

  if(!predict && main_window_bar)
  {
    // aim zone
    if(accel_mwindow_end.integer){
      // relative / absolute size switch
      if(accel_mwindow_end.integer & ACCEL_MWINDOW_END_RELATIVE_SIZE){
        window_end_size_calculated = (accel_mwindow_end_size.value / 100) * main_window_bar->iwidth * a.to_real_pixel;
      }
      else{
        window_end_size_calculated = a.window_end_size_scaled;
      }

      // applying min size
      if(window_end_size_calculated < a.window_end_min_size_scaled){
        window_end_size_calculated = a.window_end_min_size_scaled;
      }

      // size check with actual window bar
      float window_bar_width = main_window_bar->iwidth * a.to_real_pixel;
      if(window_bar_width <= window_end_size_calculated)
      {
        window_end_size_calculated = window_bar_width;
      }
      
      // potentially wrong sign ?
      vel_distance_angle = angle_short_radial_distance(a.vel_angle, a.yaw_angle + main_window_bar->angle_middle);
      if(
        (vel_distance_angle < 0 && !opposite_facing)
        || (vel_distance_angle >= 0 && opposite_facing)
      ){
        window_end_on_right_side = 1;
      }
    }
  }

  // *** draw ***
  // actual drawing si done here, for each bar in graph
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;

    do // dummy loop (wont repeat, just for "continue" in the middle)
    {
      // bar's drawing
      if(bar->polarity > 0)  // positive bar
      {
        // set colors
        if(predict){
          set_color_pred(predict);
        } else {
          if(accel.integer & ACCEL_HL_ACTIVE && bar == center_bar){ // && center_bar // unnecessary check
            set_color(COLOR_ID(accel_hl_rgba));
          }
          else {
            set_color(COLOR_ID(accel_rgba));
          }
        }
        
        height = bar->height;

        if(!predict && bar == main_window_bar){
          if(accel_mwindow_end.integer & ACCEL_MWINDOW_END_SUBTRACT && window_end_size_calculated > 0){
            // cutting the main window end off
            if(window_end_on_right_side){
              draw_positive(bar->ix * a.to_real_pixel, bar->y, bar->iwidth * a.to_real_pixel - window_end_size_calculated, height);
            }else{
              draw_positive(bar->ix * a.to_real_pixel + window_end_size_calculated, bar->y, bar->iwidth * a.to_real_pixel - window_end_size_calculated, height);
            }
          }
          else
          {
            // just regular main window bar no parts or cuts
            draw_positive(bar->ix * a.to_real_pixel, bar->y, bar->iwidth * a.to_real_pixel, height);
          }
        }
        else  // either predict or not main window bar
        {
          draw_positive(bar->ix * a.to_real_pixel, bar->y, bar->iwidth * a.to_real_pixel, height);
        }
      }
      // negative bar
      else if(bar->polarity < 0)
      {
        // we can get here only when:
        // prediction == 0
        // && (
        //  accel_neg_mode.value == ACCEL_NEGATIVE_ENABLED
        //  || accel_neg_mode.value == ACCEL_NEGATIVE_ADJECENT
        // )
        // skip non adjecent negatives
        if((accel_negative.value == ACCEL_NEGATIVE_ADJECENT)
            &&
            // is not positive left adjecent
            !(
              bar->prev && bar->prev->next_is_adj && bar->prev_is_adj // check if its rly adjecent
              && bar->prev->polarity == 1 // lets not consider 0 as positive in this case
            )
            &&
            // is not positive right adjecent
            !(
              bar->next && bar->next->prev_is_adj && bar->next_is_adj // check if its rly adjecent
              && bar->next->polarity == 1 // lets not consider 0 as positive in this case
            )
          )
        {
          // negative_draw_skip = 1;
          continue;
        }

        if(accel.integer & ACCEL_HL_ACTIVE && bar == center_bar) // && center_bar -> redundant
        {
          set_color(COLOR_ID(accel_hl_neg_rgba));
        }
        else{
          set_color(COLOR_ID(accel_neg_rgba));
        }

        // height = (bar->y - ypos_scaled); // height should not be gapped imo need test // + zero_gap_scaled
        height = fabsf(bar->height);

        draw_negative(bar->ix * a.to_real_pixel, bar->y, bar->iwidth * a.to_real_pixel, height);
      }
    } while(0); // dummy loop
  } // /for each bar

  if(predict)
  {
    // reset
    trap_R_SetColor(NULL);
    return;
  }

  // specific to main window bar so no need to have it in bar loop
  if(main_window_bar){
    // main window end
    if(accel_mwindow_end.integer){
      float window_end_offset = 0;
      // set offset based on the side
      if(window_end_on_right_side){
        // right
        window_end_offset = main_window_bar->iwidth * a.to_real_pixel - window_end_size_calculated;
      }

      // ugly but most straight forward
      if(accel_mwindow_end.integer & ACCEL_MWINDOW_END_HL
        && main_window_bar->ix * a.x_angle_ratio - window_end_offset * a.to_real_pixel_inv >= 0
        && main_window_bar->ix * a.x_angle_ratio - (window_end_offset + window_end_size_calculated) * a.to_real_pixel_inv <= 0){
        set_color(COLOR_ID(accel_window_end_hl_rgba));
      }
      else{
        set_color(COLOR_ID(accel_window_end_rgba));
      }

      const float x = main_window_bar->ix * a.to_real_pixel + window_end_offset;

      height = main_window_bar->height;

      if(accel_mwindow_end_height.value > 0){
        height = accel_mwindow_end.integer & ACCEL_MWINDOW_END_RELATIVE_HEIGHT ? (accel_mwindow_end_height.value / 100) * height : a.window_end_height_scaled;
      }

      if(accel_mwindow_end_height.value > 0 && height < a.window_end_min_height_scaled){
        height = a.window_end_min_height_scaled;
      }

      float y_target = a.hud_ypos_scaled - height;

      if(accel_mwindow_end_voffset.value > 0){
        y_target -= a.window_end_voffset_scaled;
      }
     
      draw_positive_window_end(x, y_target, window_end_size_calculated, height);
    }
  }

  
  // edges
  if(accel_edge.integer){
    int right_i_color;
    for(it = start; it && it != end->next; it = it->next)
    {
      bar = it;
      if(!(bar->polarity == 1
        && (
          !bar->prev
          || bar->prev == start->prev
          || bar->polarity != bar->prev->polarity
          || !bar->prev_is_adj
          || !bar->prev->next_is_adj
        )
      )){ continue; }

      bar_tmp = bar;
      // find end of positive window
      while(bar_tmp->next && bar_tmp->next->polarity == 1 && bar_tmp->next != end->next){
        bar_tmp = bar_tmp->next;
      }

      // special handling for single positive bar ?

      // potentially wrong (sign ?)
      vel_distance_angle = angle_short_radial_distance(a.vel_angle, a.yaw_angle + bar->angle_middle);
      if(
        (vel_distance_angle < 0 && !opposite_facing)
        || (vel_distance_angle >= 0 && opposite_facing)
      ){
        i_color = COLOR_ID(accel_near_edge_rgba);
        right_i_color = COLOR_ID(accel_far_edge_rgba);
      }
      else {
        i_color = COLOR_ID(accel_far_edge_rgba);
        right_i_color = COLOR_ID(accel_near_edge_rgba);
      }

      // get edges height per case
      float lh = 0, rh = 0;

      if(accel_edge_height.value > 0){
        if(accel_edge.integer & ACCEL_EDGE_RELATIVE_HEIGHT){
          lh = bar->height * (accel_edge_height.value / 100);
          rh = bar_tmp->height * (accel_edge_height.value / 100);
        }
        else
        {
          rh = lh = a.edge_height_scaled;
        }
      }
      else
      {
        lh = bar->height;
        rh = bar_tmp->height;
      }
      
      // apply min height // before FULL_SIZE ? 
      if(accel_edge_min_height.value > 0){
        lh = lh < a.edge_min_height_scaled ? a.edge_min_height_scaled : lh;
        rh = rh < a.edge_min_height_scaled ? a.edge_min_height_scaled : rh;
      }

      float ly = a.hud_ypos_scaled - lh,
            ry = a.hud_ypos_scaled - rh;

      if(accel_edge.integer & ACCEL_EDGE_FULL_SIZE){
        lh = lh * 2 + a.neg_offset_scaled; 
        rh = rh * 2 + a.neg_offset_scaled;
      }

      float lw = a.edge_size_scaled,
            rw = a.edge_size_scaled;

      if(accel_edge.integer & ACCEL_EDGE_RELATIVE_SIZE){
        lw = bar->iwidth * a.to_real_pixel * (accel_edge_size.value / 100);
        rw = bar_tmp->iwidth * a.to_real_pixel * (accel_edge_size.value / 100);
      }

      if(accel_edge_min_size.value > 0 && lw < a.edge_min_size_scaled){
        lw = a.edge_min_size_scaled;
      }

      if(accel_edge_min_size.value > 0 && rw < a.edge_min_size_scaled){
        rw = a.edge_min_size_scaled;
      }

      const float lx = bar->ix * a.to_real_pixel - lw,
                  rx = bar_tmp->ix * a.to_real_pixel + bar_tmp->iwidth * a.to_real_pixel;

      if(accel_edge_voffset.value > 0){
        ly -= a.edge_voffset_scaled;
        ry -= a.edge_voffset_scaled;
      }

      // left
      set_color(i_color);
      draw_positive_edge(lx, ly, lw, lh);

      // right
      set_color(right_i_color);
      draw_positive_edge(rx, ry, rw, rh);

      // skip all positive we just handled
      it = bar_tmp;
    }
  }// /edges

  trap_R_SetColor(NULL);
}

static void PM_SlickAccelerate(vec3_t wishdir, float const wishspeed, float const accel_, int predict, int predict_window)
{
  PM_Accelerate(wishdir, wishspeed, accel_, MOVE_WALK_SLICK, predict, predict_window);
}


/*
===================
PAL_AirMove

===================
*/
static void PM_AirMove( const move_data_t *move_data ) {
  int			i;
  vec3_t		wishvel;
  vec3_t		wishdir;
  float		wishspeed;
  float		scale;

  // this one is pretty interesting, because its based on speed
  scale = accel_trueness.integer & ACCEL_TN_JUMPCROUCH || predict == PREDICT_JUMPCROUCH || predict == PREDICT_JUMPCROUCH_SM ?
    PM_CmdScale(&game.pm_ps, &game.pm.cmd) :
    PM_AltCmdScale(&game.pm_ps, &game.pm.cmd);

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
  if (game.pm_ps.pm_flags & PMF_PROMODE && accel_trueness.integer & ACCEL_TN_CPM)
  {
    if(!game.pm.cmd.forwardmove && game.pm.cmd.rightmove){
      PM_Accelerate(wishdir,
        (wishspeed > cpm_airwishspeed ? cpm_airwishspeed : wishspeed),
        cpm_airstrafeaccelerate, MOVE_AIR_CPM, predict, predict_window);
    }
    else {
      PM_Accelerate(wishdir, wishspeed,
        (DotProduct(game.pm_ps.velocity, wishdir) < 0 ? 2.5f : pm_airaccelerate),
      MOVE_AIR_CPM, predict, predict_window);
    }
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate, MOVE_AIR, predict, predict_window);
  }
}


/*
===================
PAL_WalkMove

===================
*/
static void PM_WalkMove( const move_data_t *move_data ) {
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
        PM_AirMove(predict, predict_window);
    }
    return;
  }

  scale = accel_trueness.integer & ACCEL_TN_JUMPCROUCH || predict == PREDICT_JUMPCROUCH || predict == PREDICT_JUMPCROUCH_SM ?
  PM_CmdScale(&game.pm_ps, &game.pm.cmd) :
  PM_AltCmdScale(&game.pm_ps, &game.pm.cmd);

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
    // added fractions of direction (the camera once) increased over move (127 run speed)
    wishvel[i] = game.pml.forward[i]*game.pm.cmd.forwardmove + game.pml.right[i]*game.pm.cmd.rightmove;
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
      PM_SlickAccelerate(wishdir, wishspeed, game.pm_ps.pm_flags & PMF_PROMODE ? cpm_slickaccelerate : pm_slickaccelerate, predict, predict_window);
    }
    else
    {
      // don't reset the z velocity for slopes
      // a.pm_ps.velocity[2] = 0;
      PM_Accelerate(wishdir, wishspeed, game.pm_ps.pm_flags & PMF_PROMODE ? cpm_accelerate : pm_accelerate, MOVE_WALK, predict, predict_window);
    }
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate, MOVE_WALK, predict, predict_window);
  }
}
