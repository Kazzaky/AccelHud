/*
 * Author: Palme
 * Licence: GPLv3
 *
 * Code heavily use cgame_proxymode and Quake III Arena code,
 * additional licence rules may apply.
 */

 // TODO: refactor aim zone substitution

#include <float.h>
#include <stdlib.h>

#include "cg_accel.h"

#include "accel_version.h"

#include "bg_pmove.h"
#include "cg_cvar.h"
#include "cg_local.h"
#include "cg_utils.h"

#include "common.h"
#include "q_shared.h"

#define ACCEL_DEBUG 0

#define _ColorClip(a)          ((a)[0] = fmax(fmin((a)[0],1),-1), (a)[1] = fmax(fmin((a)[1],1),-1), (a)[2] = fmax(fmin((a)[2],1),-1), (a)[3] = fmax(fmin((a)[3],1),-1))
#define ColorAdd(a, b, c)      ((c)[0] = (a)[0] + (b)[0], (c)[1] = (a)[1] + (b)[1], (c)[2] = (a)[2] + (b)[2], (c)[3] = (a)[3] + (b)[3], _ColorClip(c))

static vmCvar_t accel;
static vmCvar_t accel_trueness;
static vmCvar_t accel_min_speed;
static vmCvar_t accel_base_height;
static vmCvar_t accel_max_height;
static vmCvar_t accel_vcenter_offset;
static vmCvar_t accel_yh;

static vmCvar_t accel_rgba;
static vmCvar_t accel_window_rgba;
static vmCvar_t accel_window_hl_rgba;
static vmCvar_t accel_alt_rgba;
static vmCvar_t accel_neg_rgba;
static vmCvar_t accel_near_edge_rgba;
static vmCvar_t accel_far_edge_rgba;
static vmCvar_t accel_hl_rgba;
static vmCvar_t accel_hl_neg_rgba;
static vmCvar_t accel_hl_g_adj_rgba;
static vmCvar_t accel_current_rgba;

static vmCvar_t accel_line_rgba;
static vmCvar_t accel_line_alt_rgba;
static vmCvar_t accel_line_neg_rgba;
static vmCvar_t accel_line_hl_rgba;
static vmCvar_t accel_line_hl_neg_rgba;
static vmCvar_t accel_line_hl_g_adj_rgba;

static vmCvar_t accel_line_window_rgba;
static vmCvar_t accel_line_window_hl_rgba;

static vmCvar_t accel_vline_rgba;
static vmCvar_t accel_zero_rgba;

static vmCvar_t accel_predict_strafe_rgba;
static vmCvar_t accel_predict_sidemove_rgba;
static vmCvar_t accel_predict_opposite_rgba;
static vmCvar_t accel_predict_crouchjump_rgba;

static vmCvar_t accel_window_center_rgba;
static vmCvar_t accel_line_window_center_rgba;
static vmCvar_t accel_window_center_hl_rgba;
static vmCvar_t accel_line_window_center_hl_rgba;

static vmCvar_t accel_aim_zone_rgba;
static vmCvar_t accel_line_aim_zone_rgba;
static vmCvar_t accel_aim_zone_hl_rgba;
static vmCvar_t accel_line_aim_zone_hl_rgba;

static vmCvar_t accel_line_size;
static vmCvar_t accel_vline_size;
static vmCvar_t accel_condensed_size;
static vmCvar_t accel_predict_offset;
static vmCvar_t accel_predict_crouchjump_offset;

static vmCvar_t accel_show_move;
static vmCvar_t accel_show_move_vq3;

static vmCvar_t accel_p_strafe_w_sm;
static vmCvar_t accel_p_strafe_w_fm;
static vmCvar_t accel_p_strafe_w_nk;
static vmCvar_t accel_p_strafe_w_strafe;
static vmCvar_t accel_p_cj_w_strafe;

static vmCvar_t accel_p_strafe_w_sm_vq3;
static vmCvar_t accel_p_strafe_w_fm_vq3;
static vmCvar_t accel_p_strafe_w_nk_vq3;
static vmCvar_t accel_p_strafe_w_strafe_vq3;
static vmCvar_t accel_p_cj_w_strafe_vq3;
static vmCvar_t accel_p_cj_w_sm_vq3;

static vmCvar_t accel_p_sm_w_sm_vq3;
static vmCvar_t accel_p_sm_w_strafe_vq3;
static vmCvar_t accel_p_sm_w_fm_vq3;
static vmCvar_t accel_p_sm_w_nk_vq3;

static vmCvar_t accel_crouchjump_overdraw;

static vmCvar_t accel_mode_neg;

static vmCvar_t accel_vline;

static vmCvar_t version;

static vmCvar_t accel_threshold; // -0.38 was the biggest seen plasma climb

static vmCvar_t accel_window_threshold;
static vmCvar_t accel_window_grow_limit;

static vmCvar_t accel_point_line_size;

static vmCvar_t accel_merge_threshold; // width threshold not delta !

static vmCvar_t accel_edge;
static vmCvar_t accel_edge_size;
static vmCvar_t accel_edge_min_size;
static vmCvar_t accel_edge_height;
static vmCvar_t accel_edge_min_height;
static vmCvar_t accel_edge_voffset;

static vmCvar_t accel_window_center;
static vmCvar_t accel_window_center_size;
static vmCvar_t accel_window_center_min_size;
static vmCvar_t accel_window_center_height;
static vmCvar_t accel_window_center_min_height;
static vmCvar_t accel_window_center_voffset;

static vmCvar_t accel_aim_zone;
static vmCvar_t accel_aim_zone_size;
static vmCvar_t accel_aim_zone_min_size;
static vmCvar_t accel_aim_zone_height;
static vmCvar_t accel_aim_zone_min_height;
static vmCvar_t accel_aim_zone_voffset;


#if ACCEL_DEBUG
  static vmCvar_t accel_verbose;
#endif // ACCEL_DEBUG


static cvarTable_t accel_cvars[] = {
  { &accel, "p_accel", "0b000000000000000", CVAR_ARCHIVE_ND },

//#define ACCEL_DISABLED            0
#define ACCEL_ENABLE              1 // the basic view
#define ACCEL_HL_ACTIVE           2 // highlight active
#define ACCEL_LINE_ACTIVE         4 // border line active
#define ACCEL_DISABLE_BAR_AREA    8 // highlight active
#define ACCEL_VL_ACTIVE           16 // vertical line active
#define ACCEL_PL_ACTIVE           32 // point line active
#define ACCEL_CB_ACTIVE           64 // condensed bar active
#define ACCEL_UNIFORM_VALUE       128 // uniform values
#define ACCEL_HL_G_ADJ            256 // highlight greater adjecent
#define ACCEL_WINDOW              512 // draw only window bar
#define ACCEL_CUSTOM_WINDOW_COL   1024 // draw window with custom color
#define ACCEL_HL_WINDOW           2048 // when at window highlight it
#define ACCEL_NEG_UP              4096 // negatives grow up (not down as default)
#define ACCEL_COLOR_ALTERNATE     8192 // alternating positive colors
#define ACCEL_VCENTER             16384 // apply vertical bar centering


  { &accel_trueness, "p_accel_trueness", "0b0000", CVAR_ARCHIVE_ND },

#define ACCEL_TN_JUMPCROUCH      1
#define ACCEL_TN_CPM             2
#define ACCEL_TN_GROUND          4
#define ACCEL_TN_STATIC_BOOST    8 
// 1000 ups -> delta ~1.5f, 3000 ups -> delta ~ 1.0f, 5000 ups -> delta ~ 0.5f

  { &accel_min_speed, "p_accel_min_speed", "0", CVAR_ARCHIVE_ND },
  { &accel_base_height, "p_accel_base_height", "0", CVAR_ARCHIVE_ND },
  { &accel_max_height, "p_accel_max_height", "50", CVAR_ARCHIVE_ND },
  { &accel_vcenter_offset, "p_accel_vcenter_offset", "15", CVAR_ARCHIVE_ND },
  { &accel_yh, "p_accel_yh", "180 30", CVAR_ARCHIVE_ND },

  { &accel_rgba, "p_accel_rgba", ".2 .9 .2 .5", CVAR_ARCHIVE_ND },
  { &accel_window_rgba, "p_accel_window_rgba", ".5 1 .6 .6", CVAR_ARCHIVE_ND },
  { &accel_window_hl_rgba, "p_accel_window_hl_rgba", ".6 1 .7 .8", CVAR_ARCHIVE_ND },
  { &accel_alt_rgba, "p_accel_alt_rgba", ".4 .1 .7 .5", CVAR_ARCHIVE_ND },
  { &accel_neg_rgba, "p_accel_neg_rgba", ".9 .2 .2 .5", CVAR_ARCHIVE_ND },
  { &accel_near_edge_rgba, "p_accel_near_edge_rgba", ".7 .1 .1 .5", CVAR_ARCHIVE_ND },
  { &accel_far_edge_rgba, "p_accel_far_edge_rgba", ".1 .1 .7 .5", CVAR_ARCHIVE_ND },
  { &accel_hl_rgba, "p_accel_hl_rgba", ".3 1 .3 .75", CVAR_ARCHIVE_ND },
  { &accel_hl_neg_rgba, "p_accel_hl_neg_rgba", ".9 .3 .3 .75", CVAR_ARCHIVE_ND },
  { &accel_current_rgba, "p_accel_cur_rgba", ".9 .1 .9 .75", CVAR_ARCHIVE_ND },
  { &accel_line_rgba, "p_accel_line_rgba", ".4 .9 .4 .6", CVAR_ARCHIVE_ND },
  { &accel_line_alt_rgba, "p_accel_line_alt_rgba", ".7 .4 .9 .6", CVAR_ARCHIVE_ND },
  { &accel_line_neg_rgba, "p_accel_line_neg_rgba", ".9 .5 .5 .6", CVAR_ARCHIVE_ND },
  { &accel_line_hl_rgba, "p_accel_line_hl_rgba", ".6 1 .6 .8", CVAR_ARCHIVE_ND },
  { &accel_line_hl_neg_rgba, "p_accel_line_hl_neg_rgba", "1 .6 .6 .8", CVAR_ARCHIVE_ND },
  { &accel_vline_rgba, "p_accel_vline_rgba", ".1 .1 .9 .6", CVAR_ARCHIVE_ND },
  { &accel_zero_rgba, "p_accel_zero_rgba", ".1 .1 .9 .6", CVAR_ARCHIVE_ND },
  { &accel_hl_g_adj_rgba, "p_accel_hl_g_adj_rgba", ".9 .55 .0 .8", CVAR_ARCHIVE_ND },
  { &accel_line_hl_g_adj_rgba, "p_accel_line_hl_g_adj_rgba", "1 .55 .2 .8", CVAR_ARCHIVE_ND },
  { &accel_line_window_rgba, "p_accel_line_window_rgba", ".6 1 .7 .7", CVAR_ARCHIVE_ND },
  { &accel_line_window_hl_rgba, "p_accel_line_window_hl_rgba", ".7 1 .8 .9", CVAR_ARCHIVE_ND },


  { &accel_predict_strafe_rgba, "p_accel_p_strafe_rgbam", "-.2 -.1 .4 -.4", CVAR_ARCHIVE_ND },
  { &accel_predict_sidemove_rgba, "p_accel_p_sm_rgbam", ".4 -.1 -.2 -.4", CVAR_ARCHIVE_ND },
  { &accel_predict_opposite_rgba, "p_accel_p_opposite_rgbam", ".8 .-8 .8 -.3", CVAR_ARCHIVE_ND }, // 1.0 0.9 0.2 0.6
  { &accel_predict_crouchjump_rgba, "p_accel_p_cj_rgbam", "1 1 1 1", CVAR_ARCHIVE_ND },

  { &accel_window_center_rgba, "p_accel_window_center_rgba", "1 .4 .2 .9", CVAR_ARCHIVE_ND },
  { &accel_line_window_center_rgba, "p_accel_line_window_center_rgba", ".9 .3 .3 .9", CVAR_ARCHIVE_ND },
    { &accel_window_center_hl_rgba, "p_accel_window_center_hl_rgba", ".9 .6 .3 .9", CVAR_ARCHIVE_ND },
  { &accel_line_window_center_hl_rgba, "p_accel_line_window_center_hl_rgba", ".8 .5 .5 .9", CVAR_ARCHIVE_ND },

  { &accel_aim_zone_rgba, "p_accel_aim_zone_rgba", ".2 .4 1 .9", CVAR_ARCHIVE_ND },
  { &accel_line_aim_zone_rgba, "p_accel_line_aim_zone_rgba", ".3 .3 .9 .9", CVAR_ARCHIVE_ND },
    { &accel_aim_zone_hl_rgba, "p_accel_aim_zone_hl_rgba", ".3 .6 .9 .9", CVAR_ARCHIVE_ND },
  { &accel_line_aim_zone_hl_rgba, "p_accel_line_aim_zone_hl_rgba", ".5 .5 .8 .9", CVAR_ARCHIVE_ND },

  // add new colors here 
  
  { &accel_line_size, "p_accel_line_size", "5", CVAR_ARCHIVE_ND },
  { &accel_vline_size, "p_accel_vline_size", "1", CVAR_ARCHIVE_ND },
  { &accel_condensed_size, "p_accel_cond_size", "3", CVAR_ARCHIVE_ND },
  { &accel_predict_offset, "p_accel_p_offset", "30", CVAR_ARCHIVE_ND },
  { &accel_predict_crouchjump_offset, "p_accel_p_cj_offset", "0", CVAR_ARCHIVE_ND },

  // enable regular accel graph while holding specific keys
  { &accel_show_move, "p_accel_show_move", "0b1101", CVAR_ARCHIVE_ND },
  { &accel_show_move_vq3, "p_accel_show_move_vq3", "0b1111", CVAR_ARCHIVE_ND },

  #define ACCEL_MOVE_STRAFE         1
  #define ACCEL_MOVE_SIDE           2
  #define ACCEL_MOVE_FORWARD        4
  #define ACCEL_MOVE_SIDE_GROUNDED  8

  { &accel_p_strafe_w_sm,     "p_accel_p_strafe_w_sm", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_strafe_w_fm,     "p_accel_p_strafe_w_fm", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_strafe_w_nk,     "p_accel_p_strafe_w_nk", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_strafe_w_strafe, "p_accel_p_strafe_w_strafe", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_cj_w_strafe,     "p_accel_p_cj_w_strafe", "0b00", CVAR_ARCHIVE_ND },

  { &accel_p_strafe_w_sm_vq3,     "p_accel_p_strafe_w_sm_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_strafe_w_fm_vq3,     "p_accel_p_strafe_w_fm_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_strafe_w_nk_vq3,     "p_accel_p_strafe_w_nk_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_strafe_w_strafe_vq3, "p_accel_p_strafe_w_strafe_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_cj_w_strafe_vq3,     "p_accel_p_cj_w_strafe_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_cj_w_sm_vq3,         "p_accel_p_cj_w_sm_vq3", "0b00", CVAR_ARCHIVE_ND },

  { &accel_p_sm_w_sm_vq3, "p_accel_p_sm_w_sm_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_sm_w_strafe_vq3, "p_accel_p_sm_w_strafe_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_sm_w_fm_vq3, "p_accel_p_sm_w_fm_vq3", "0b00", CVAR_ARCHIVE_ND },
  { &accel_p_sm_w_nk_vq3, "p_accel_p_sm_w_nk_vq3", "0b00", CVAR_ARCHIVE_ND },


#define ACCEL_MOVE_PREDICT          1
#define ACCEL_MOVE_PREDICT_WINDOW   2


  { &accel_crouchjump_overdraw, "p_accel_p_cj_overdraw", "0", CVAR_ARCHIVE_ND },


  { &accel_mode_neg, "p_accel_neg_mode", "0", CVAR_ARCHIVE_ND },

//#define ACCEL_NEG_MODE          0 // negative disabled
#define ACCEL_NEG_MODE_ENABLED    1 // negative enabled
#define ACCEL_NEG_MODE_ADJECENT   2 // only adjecent negative are shown
//#define ACCEL_NEG_MODE_ZERO_ONLY  3 // calculated but only at zero bar shown

  { &accel_vline, "p_accel_vline", "0b00", CVAR_ARCHIVE_ND },

#define ACCEL_VL_USER_COLOR   1 // line have user defined color, if this is not set the colors are pos/neg
#define ACCEL_VL_LINE_H       2 // include bar height

  { &version, "p_accel_version", ACCEL_VERSION, CVAR_USERINFO | CVAR_INIT },

  { &accel_threshold, "p_accel_threshold", "0", CVAR_ARCHIVE_ND },

  { &accel_window_threshold, "p_accel_window_threshold", "10", CVAR_ARCHIVE_ND },
  { &accel_window_grow_limit, "p_accel_window_grow_limit", "5", CVAR_ARCHIVE_ND },

  { &accel_point_line_size, "p_accel_point_line_size", "1", CVAR_ARCHIVE_ND },

{ &accel_merge_threshold, "p_accel_merge_threshold", "2", CVAR_ARCHIVE_ND },

{ &accel_edge, "p_accel_edge", "0b000000", CVAR_ARCHIVE_ND },

//#define ACCEL_EDGE_ENABLE 1
#define ACCEL_EDGE_FULL_SIZE        2 // extend to negative (double size + gap)
#define ACCEL_EDGE_RELATIVE_SIZE    4 // making p_accel_edge_size percentage of bar width
#define ACCEL_EDGE_RELATIVE_HEIGHT  8 // making p_accel_edge_height percentage of bar height
#define ACCEL_EDGE_VCENTER_FORCE    16 // override regular accel graph vcentering
#define ACCEL_EDGE_VCENTER          32 // forced vcentering

{ &accel_edge_size, "p_accel_edge_size", "1", CVAR_ARCHIVE_ND },
{ &accel_edge_min_size, "p_accel_edge_min_size", "2", CVAR_ARCHIVE_ND },
{ &accel_edge_height, "p_accel_edge_height", "-1", CVAR_ARCHIVE_ND },
{ &accel_edge_min_height, "p_accel_edge_min_height", "-1", CVAR_ARCHIVE_ND },
{ &accel_edge_voffset, "p_accel_edge_voffset", "0", CVAR_ARCHIVE_ND },

{ &accel_window_center, "p_accel_window_center", "0b000000", CVAR_ARCHIVE_ND },

#define ACCEL_WINDOW_CENTER_HL              2  // highlight 
#define ACCEL_WINDOW_CENTER_RELATIVE_SIZE   4  // making p_accel_window_center_size percentage of bar width
#define ACCEL_WINDOW_CENTER_RELATIVE_HEIGHT 8  // making p_accel_window_center_height percentage of bar height
#define ACCEL_WINDOW_CENTER_VCENTER_FORCE   16 // override regular accel graph vcentering
#define ACCEL_WINDOW_CENTER_VCENTER         32 // forced vcentering

{ &accel_window_center_size, "p_accel_window_center_size", "10", CVAR_ARCHIVE_ND },
{ &accel_window_center_min_size, "p_accel_window_center_min_size", "5", CVAR_ARCHIVE_ND },
{ &accel_window_center_height, "p_accel_window_center_height", "-1", CVAR_ARCHIVE_ND },
{ &accel_window_center_min_height, "p_accel_window_center_min_height", "0", CVAR_ARCHIVE_ND },
{ &accel_window_center_voffset, "p_accel_window_center_voffset", "0", CVAR_ARCHIVE_ND },

{ &accel_aim_zone, "p_accel_aim_zone", "0b0000000", CVAR_ARCHIVE_ND },

#define ACCEL_AIM_HL              2   // highlight
#define ACCEL_AIM_RELATIVE_SIZE   4   // making p_accel_aim_zone_size percentage of bar width
#define ACCEL_AIM_RELATIVE_HEIGHT 8   // making p_accel_aim_zone_height percentage of bar height
#define ACCEL_AIM_SUBTRACT        16  // cutoff the "covered" area of window zone
#define ACCEL_AIM_VCENTER_FORCE   32  // override regular accel graph vcentering
#define ACCEL_AIM_VCENTER         64  // forced vcentering

{ &accel_aim_zone_size, "p_accel_aim_zone_size", "10", CVAR_ARCHIVE_ND },
{ &accel_aim_zone_min_size, "p_accel_aim_zone_min_size", "10", CVAR_ARCHIVE_ND },
{ &accel_aim_zone_height, "p_accel_aim_zone_height", "-1", CVAR_ARCHIVE_ND },
{ &accel_aim_zone_min_height, "p_accel_aim_zone_min_height", "-1", CVAR_ARCHIVE_ND },
{ &accel_aim_zone_voffset, "p_accel_aim_zone_voffset", "0", CVAR_ARCHIVE_ND },



  #if ACCEL_DEBUG
    { &accel_verbose, "p_accel_verbose", "0", CVAR_ARCHIVE_ND },
  #endif // ACCEL_DEBUG
};

enum {
  RGBA_I_POS,
  RGBA_I_WINDOW,
  RGBA_I_WINDOW_HL,
  RGBA_I_ALT,
  RGBA_I_NEG,
  RGBA_I_EDGE_NEAR,
  RGBA_I_EDGE_FAR,
  RGBA_I_HL_POS,
  RGBA_I_HL_NEG,
  RGBA_I_POINT,
  RGBA_I_BORDER,
  RGBA_I_BORDER_ALT,
  RGBA_I_BORDER_NEG,
  RGBA_I_BORDER_HL_POS,
  RGBA_I_BORDER_HL_NEG,
  RGBA_I_VLINE,
  RGBA_I_ZERO,
  RGBA_I_HL_G_ADJ,
  RGBA_I_BORDER_HL_G_ADJ,
  RGBA_I_BORDER_WIDNOW,
  RGBA_I_BORDER_WIDNOW_HL,
  RGBA_I_PREDICT_WAD,
  RGBA_I_PREDICT_AD,
  RGBA_I_PREDICT_OPPOSITE,
  RGBA_I_PREDICT_CROUCHJUMP,
  RGBA_I_WINDOW_CENTER,
  RGBA_I_BORDER_WINDOW_CENTER,
  RGBA_I_WINDOW_CENTER_HL,
  RGBA_I_BORDER_WINDOW_CENTER_HL,
  RGBA_I_AIM_ZONE,
  RGBA_I_BORDER_AIM_ZONE,
  RGBA_I_AIM_ZONE_HL,
  RGBA_I_BORDER_AIM_ZONE_HL,
  RGBA_I_LENGTH
};

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

static int move_type;

// no help here, just because there is no space in the proxymod help table and i don't want to modify it (for now)

typedef struct graph_bar_ {
  int   order; // control variable for adjecent checking
  float x; // scaled x 
  float y; // scaled y
  float height; // scaled height (not absolute -> not always positive)
  float width; // scaled width
  float value; // raw delta speed (combined)
  int   polarity; // 1 = positive, 0 = zero, -1 = negative
  float angle; // yaw relative
  struct graph_bar_ *next;
  struct graph_bar_ *prev;
} graph_bar;

#define GRAPH_MAX_RESOLUTION 3840 // 4K hardcoded

static graph_bar accel_graph[GRAPH_MAX_RESOLUTION]; // only one graph is plotted at a time
static int accel_graph_size;

static int resolution;
static float resolution_ratio;
static int center;
static float x_angle_ratio;
static float ypos_scaled;
static float hud_height_scaled;
static float zero_gap_scaled;
static float line_size_scaled;
static float vline_size_scaled;
static float predict_offset_scaled;
static float predict_crouchjump_offset_scaled;
static float vel_angle;
static float yaw;
static float edge_size_scaled;
static float edge_min_size_scaled;
static float edge_height_scaled;
static float edge_min_height_scaled;
static float edge_voffset_scaled;
static float window_center_size_scaled;
static float window_center_min_size_scaled;
static float window_center_height_scaled;
static float window_center_min_height_scaled;
static float window_center_voffset_scaled;
static float aim_zone_size_scaled;
static float aim_zone_min_size_scaled;
static float aim_zone_height_scaled;
static float aim_zone_min_height_scaled;
static float aim_zone_voffset_scaled;
static float base_height_scaled;
static float max_height_scaled;
static float vcenter_offset_scaled;


static vec4_t color_tmp;

static int predict;
static int predict_window;
static int order;

static int color_alternate;

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
}


void update_accel(void)
{
  update_cvars(accel_cvars, ARRAY_LEN(accel_cvars));
  accel.integer           = cvar_getInteger("p_accel"); // this is only relevant to be updated before draw
}

static void PmoveSingle(void);
static void PM_AirMove(void);
static void PM_WalkMove(void);
inline static float angle_short_radial_distance(float a_, float b_);

void draw_accel(void)
{
  if (!accel.integer) return;

  // update cvars
  accel_trueness.integer  = cvar_getInteger("p_accel_trueness");
  accel_vline.integer     = cvar_getInteger("p_accel_vline");

  accel_show_move.integer = cvar_getInteger("p_accel_show_move");
  accel_show_move_vq3.integer = cvar_getInteger("p_accel_show_move_vq3");

  accel_p_strafe_w_sm.integer = cvar_getInteger("p_accel_p_strafe_w_sm");
  accel_p_strafe_w_fm.integer = cvar_getInteger("p_accel_p_strafe_w_fm");
  accel_p_strafe_w_nk.integer = cvar_getInteger("p_accel_p_strafe_w_nk");
  accel_p_strafe_w_strafe.integer = cvar_getInteger("p_accel_p_strafe_w_strafe");
  accel_p_cj_w_strafe.integer = cvar_getInteger("p_accel_p_cj_w_strafe");

  accel_p_strafe_w_sm_vq3.integer = cvar_getInteger("p_accel_p_strafe_w_sm_vq3");
  accel_p_strafe_w_fm_vq3.integer = cvar_getInteger("p_accel_p_strafe_w_fm_vq3");
  accel_p_strafe_w_nk_vq3.integer = cvar_getInteger("p_accel_p_strafe_w_nk_vq3");
  accel_p_strafe_w_strafe_vq3.integer = cvar_getInteger("p_accel_p_strafe_w_strafe_vq3");
  accel_p_cj_w_strafe_vq3.integer = cvar_getInteger("p_accel_p_cj_w_strafe_vq3");
  accel_p_cj_w_sm_vq3.integer = cvar_getInteger("p_accel_p_cj_w_sm_vq3");

  accel_p_sm_w_sm_vq3.integer = cvar_getInteger("p_accel_p_sm_w_sm_vq3");
  accel_p_sm_w_strafe_vq3.integer = cvar_getInteger("p_accel_p_sm_w_strafe_vq3");
  accel_p_sm_w_fm_vq3.integer = cvar_getInteger("p_accel_p_sm_w_fm_vq3");
  accel_p_sm_w_nk_vq3.integer = cvar_getInteger("p_accel_p_sm_w_nk_vq3");

  accel_window_grow_limit.integer = cvar_getInteger("p_accel_window_grow_limit");

  accel_edge.integer  = cvar_getInteger("p_accel_edge");

  accel_window_center.integer = cvar_getInteger("p_accel_window_center");

  accel_aim_zone.integer  = cvar_getInteger("p_accel_aim_zone");

  ParseVec(accel_yh.string, a.graph_yh, 2);
  for (int i = 0; i < RGBA_I_LENGTH; ++i) ParseVec(accel_cvars[7 + i].vmCvar->string, a.graph_rgba[i], 4);

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

  // clear all pmove local vars
  memset(&a.pml, 0, sizeof(a.pml));

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

  // needed for PM_GroundTrace
  a.pm.tracemask = a.pm_ps.pm_type == PM_DEAD ? MASK_PLAYERSOLID & ~CONTENTS_BODY : MASK_PLAYERSOLID;

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
  resolution_ratio = GRAPH_MAX_RESOLUTION < cgs.glconfig.vidWidth ?
      cgs.glconfig.vidWidth / (float)GRAPH_MAX_RESOLUTION
      : 1.f;
  x_angle_ratio = cg.refdef.fov_x / resolution;
  ypos_scaled = a.graph_yh[0] * cgs.screenXScale;
  hud_height_scaled = a.graph_yh[1] * cgs.screenXScale;
  zero_gap_scaled = accel_condensed_size.value  * cgs.screenXScale;
  line_size_scaled = accel_line_size.value * cgs.screenXScale;
  vline_size_scaled = accel_vline_size.value * cgs.screenXScale;
  predict_offset_scaled = accel_predict_offset.value * cgs.screenXScale;
  predict_crouchjump_offset_scaled = accel_predict_crouchjump_offset.value * cgs.screenXScale;
  vel_angle = atan2f(a.pm_ps.velocity[1], a.pm_ps.velocity[0]);
  yaw = DEG2RAD(a.pm_ps.viewangles[YAW]);
  color_alternate = 0;
  edge_size_scaled = accel_edge_size.value * cgs.screenXScale;
  edge_min_size_scaled = accel_edge_min_size.value * cgs.screenXScale;
  edge_height_scaled = accel_edge_height.value * cgs.screenXScale;
  edge_min_height_scaled = accel_edge_min_height.value * cgs.screenXScale;
  edge_voffset_scaled = accel_edge_voffset.value * cgs.screenXScale;
  window_center_size_scaled = accel_window_center_size.value * cgs.screenXScale;
  window_center_min_size_scaled = accel_window_center_min_size.value * cgs.screenXScale;
  window_center_height_scaled = accel_window_center_height.value * cgs.screenXScale;
  window_center_min_height_scaled = accel_window_center_min_height.value * cgs.screenXScale;
  window_center_voffset_scaled = accel_window_center_voffset.value * cgs.screenXScale;
  aim_zone_size_scaled = accel_aim_zone_size.value * cgs.screenXScale;
  aim_zone_min_size_scaled = accel_aim_zone_min_size.value * cgs.screenXScale;
  aim_zone_height_scaled = accel_aim_zone_height.value * cgs.screenXScale;
  aim_zone_min_height_scaled = accel_aim_zone_min_height.value * cgs.screenXScale;
  aim_zone_voffset_scaled = accel_aim_zone_voffset.value * cgs.screenXScale;
  base_height_scaled = accel_base_height.value * cgs.screenXScale;
  max_height_scaled = accel_max_height.value * cgs.screenXScale;
  vcenter_offset_scaled = accel_vcenter_offset.value * cgs.screenXScale;


  predict = 0;
  predict_window = 0;
  order = 0; // intentionally here in case we predict both sides to prevent the rare case of adjecent false positive

  signed char key_forwardmove = a.pm.cmd.forwardmove,
      key_rightmove = a.pm.cmd.rightmove,
      key_upmove = a.pm.cmd.upmove;

  // * predictions *

  int fm_case;

  // cpm
  if(a.pm_ps.pm_flags & PMF_PROMODE)
  {
    if(accel_p_strafe_w_sm.integer && !key_forwardmove && key_rightmove){
      // strafe predict
      a.pm.cmd.forwardmove = scale;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_SM_STRAFE;
      predict_window = accel_p_strafe_w_sm.integer & ACCEL_MOVE_PREDICT_WINDOW;
      move();
      // opposite side
      a.pm.cmd.rightmove *= -1;
      move();
    }

    fm_case = accel_p_strafe_w_fm.integer & ACCEL_MOVE_PREDICT && key_forwardmove && !key_rightmove;
    if(fm_case || (accel_p_strafe_w_nk.integer && !key_forwardmove && !key_rightmove)){
      // strafe predict
      a.pm.cmd.forwardmove = scale;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_FMNK_STRAFE;
      predict_window = fm_case ? accel_p_strafe_w_fm.integer & ACCEL_MOVE_PREDICT_WINDOW : accel_p_strafe_w_nk.integer & ACCEL_MOVE_PREDICT_WINDOW;
      move();
      // opposite side
      a.pm.cmd.rightmove *= -1;
      move();
      // return; // no longer in use as we have show_move
    }

    // predict same move just opposite side
    if(accel_p_strafe_w_strafe.integer && key_forwardmove && key_rightmove)
    {
      predict_window = accel_p_strafe_w_strafe.integer & ACCEL_MOVE_PREDICT_WINDOW;
      predict = PREDICT_OPPOSITE;
      a.pm.cmd.forwardmove = scale; // why not key_forwardmove here ?
      a.pm.cmd.rightmove = key_rightmove * -1;
      move();
    }
  }
  else // vq3
  {
    if(accel_p_strafe_w_sm_vq3.integer && !key_forwardmove && key_rightmove){
      // strafe predict
      a.pm.cmd.forwardmove = scale;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_SM_STRAFE_ADD;
      predict_window = accel_p_strafe_w_sm_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
      move();
      a.pm.cmd.rightmove *= -1;
      move();
    }

    fm_case = accel_p_strafe_w_fm_vq3.integer & ACCEL_MOVE_PREDICT && key_forwardmove && !key_rightmove;
    if(fm_case || (accel_p_strafe_w_nk_vq3.integer && !key_forwardmove && !key_rightmove)){
      // strafe predict
      a.pm.cmd.forwardmove = scale;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_FMNK_STRAFE;
      predict_window = fm_case ? accel_p_strafe_w_fm_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW : accel_p_strafe_w_nk_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
      move();
      // opposite side
      a.pm.cmd.rightmove *= -1;
      move();
    }

    fm_case = accel_p_sm_w_fm_vq3.integer & ACCEL_MOVE_PREDICT && key_forwardmove && !key_rightmove;
    if(fm_case || (accel_p_sm_w_nk_vq3.integer && !key_forwardmove && !key_rightmove)){
      // sidemove predict
      predict_window = fm_case ? accel_p_sm_w_fm_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW : accel_p_sm_w_nk_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
      predict = PREDICT_FMNK_SM;
      a.pm.cmd.forwardmove = 0;
      a.pm.cmd.rightmove   = scale;
      move();
      a.pm.cmd.rightmove *= -1;
      move();
    }

    // predict same move just opposite side
    if(accel_p_strafe_w_strafe_vq3.integer && key_forwardmove && key_rightmove)
    {
      predict_window = accel_p_strafe_w_strafe_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
      predict = PREDICT_OPPOSITE;
      a.pm.cmd.forwardmove = scale; // why not key_forwardmove here ?
      a.pm.cmd.rightmove = key_rightmove * -1;
      move();
    }

    // predict same move just opposite side
    if(accel_p_sm_w_sm_vq3.integer && !key_forwardmove && key_rightmove)
    {
      predict_window = accel_p_sm_w_sm_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
      // a/d oposite only for vq3
      predict = PREDICT_OPPOSITE;
      a.pm.cmd.forwardmove = 0; // why not key_forwardmove here ?
      a.pm.cmd.rightmove = key_rightmove * -1;
      move();
    }

    if(accel_p_sm_w_strafe_vq3.integer && key_forwardmove && key_rightmove){
      // the sidemove predict
      a.pm.cmd.forwardmove = 0;
      a.pm.cmd.rightmove   = scale;
      predict = PREDICT_STRAFE_SM;
      predict_window = accel_p_sm_w_strafe_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
      move();
      a.pm.cmd.rightmove *= -1;
      move();
    }
  }

  // crouchjump overdraw
  if(!accel_crouchjump_overdraw.value){
    LABEL_CJ_OVERDRAW: 
    // cpm
    if(a.pm_ps.pm_flags & PMF_PROMODE)
    {
      // predict same move while jumping / crouching 
      if(accel_p_cj_w_strafe.integer && key_forwardmove && key_rightmove)
      {
        predict_window = accel_p_cj_w_strafe.integer & ACCEL_MOVE_PREDICT_WINDOW;
        predict = PREDICT_CROUCHJUMP;
        a.pm.cmd.forwardmove = key_forwardmove;
        a.pm.cmd.rightmove = key_rightmove;
        a.pm.cmd.upmove = scale;
        move();
      }
    }
    else // vq3
    {
      // predict same move while jumping / crouching // following block is doubled with different accel_crouchjump_overdraw after regular move
      if(accel_p_cj_w_strafe_vq3.integer && key_forwardmove && key_rightmove)
      {
        predict_window = accel_p_cj_w_strafe_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
        predict = PREDICT_CROUCHJUMP;
        a.pm.cmd.forwardmove = key_forwardmove;
        a.pm.cmd.rightmove = key_rightmove;
        a.pm.cmd.upmove = scale;
        move();
      }

      // predict same move while jumping / crouching // following block is doubled with different accel_crouchjump_overdraw after regular move
      if(accel_p_cj_w_sm_vq3.integer && !key_forwardmove && key_rightmove)
      {
        predict_window = accel_p_cj_w_sm_vq3.integer & ACCEL_MOVE_PREDICT_WINDOW;
        // a/d only for vq3
        predict = PREDICT_CROUCHJUMP;
        a.pm.cmd.forwardmove = key_forwardmove;
        a.pm.cmd.rightmove = key_rightmove;
        a.pm.cmd.upmove = scale;
        move();
      }
    }

    if(accel_crouchjump_overdraw.value){
      // in case we get here by goto overdraw
      return;
    }
  }


  if((key_forwardmove && key_rightmove // strafe
        && (
          (a.pm_ps.pm_flags & PMF_PROMODE && !(accel_show_move.integer & ACCEL_MOVE_STRAFE))
          || (!(a.pm_ps.pm_flags & PMF_PROMODE) && !(accel_show_move_vq3.integer & ACCEL_MOVE_STRAFE))
        ))
      || (!key_forwardmove && key_rightmove // sidemove
        && (
          (a.pm_ps.pm_flags & PMF_PROMODE // cpm
            && (
              (!a.pml.walking && !(accel_show_move.integer & ACCEL_MOVE_SIDE))
              || (a.pml.walking && !(accel_show_move.integer & ACCEL_MOVE_SIDE_GROUNDED))
            )
          )
          || (!(a.pm_ps.pm_flags & PMF_PROMODE) // vq3
            && (
              (!a.pml.walking && !(accel_show_move_vq3.integer & ACCEL_MOVE_SIDE))
              || (a.pml.walking && !(accel_show_move_vq3.integer & ACCEL_MOVE_SIDE_GROUNDED))
            )
          )
        ))
      || (key_forwardmove && !key_rightmove // forwardmove
        && (
          (a.pm_ps.pm_flags & PMF_PROMODE && !(accel_show_move.integer & ACCEL_MOVE_FORWARD))
          || (!(a.pm_ps.pm_flags & PMF_PROMODE) && !(accel_show_move_vq3.integer & ACCEL_MOVE_FORWARD))
        ))
  ){
    if(accel_crouchjump_overdraw.value){
      goto LABEL_CJ_OVERDRAW;
    }
    return; // -> regular move is disabled
  }

  // restore original keys
  a.pm.cmd.forwardmove = key_forwardmove;
  a.pm.cmd.rightmove   = key_rightmove;
  a.pm.cmd.upmove      = key_upmove;

  // reset predict
  predict = PREDICT_NONE;
  predict_window = 0;

  // regular move
  move();
 
  if(accel_crouchjump_overdraw.value){
    goto LABEL_CJ_OVERDRAW;
  }
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
    case PREDICT_CROUCHJUMP:{
      ColorAdd(a.graph_rgba[regular], a.graph_rgba[RGBA_I_PREDICT_CROUCHJUMP], color_tmp);
      trap_R_SetColor(color_tmp);
      break;
    }
    case 0:
    default:{
      trap_R_SetColor(a.graph_rgba[regular]);
    }
  }
}

inline static void add_projection_x(float *x, float *w)
{
  if(mdd_projection.integer == 1) return;

  const float half_fov_x = cg.refdef.fov_x / 2;
  const float half_screen_width = cgs.glconfig.vidWidth / 2.f;

  float angle = 0;
  float proj_x = 0, proj_w = 0;

  switch(mdd_projection.integer){
    case 0:
      angle = (*x / half_screen_width - 1) * half_fov_x;
      proj_x = half_screen_width * (1 + tanf(angle) / tanf(half_fov_x));
      angle = ((*x + *w) / half_screen_width - 1) * half_fov_x;
      proj_w = (half_screen_width * (1 + tanf(angle) / tanf(half_fov_x))) - proj_x;
      break;
    // case 1:
    //   angle = (*x / half_screen_width - 1) * half_fov_x;
    //   proj_x = half_screen_width * (1 + angle / half_fov_x);
    //   angle = ((*x + *w) / half_screen_width - 1) * half_fov_x;
    //   proj_w = (half_screen_width * (1 + angle / half_fov_x)) - proj_x;
    //   break;
    case 2:
      angle = (*x / half_screen_width - 1) * half_fov_x;
      proj_x = half_screen_width * (1 + tanf(angle / 2) / tanf(half_fov_x / 2));
      angle = ((*x + *w) / half_screen_width - 1) * half_fov_x;
      proj_w = (half_screen_width * (1 + tanf(angle / 2) / tanf(half_fov_x / 2))) - proj_x;
      break;
  }

  *x = proj_x;
  *w = proj_w;
}

// does not set color
// automatic vertical centering
inline static void draw_positive(float x, float y, float w, float h)
{
  add_projection_x(&x, &w);

  float y_target = y - zero_gap_scaled / 2;

  if(predict){
    y_target -= predict == PREDICT_CROUCHJUMP ? predict_crouchjump_offset_scaled : predict_offset_scaled;
  }

  if(accel.integer & ACCEL_VCENTER){
    y_target -= vcenter_offset_scaled - h / 2; // is h always positible ? might need abs here
  }

  trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// does not set color
// custom vertical centering
inline static void draw_positive_vc(float x, float y, float w, float h, float vh)
{
  add_projection_x(&x, &w);

  float y_target = y - zero_gap_scaled / 2;

  if(predict){
    y_target -= predict == PREDICT_CROUCHJUMP ? predict_crouchjump_offset_scaled : predict_offset_scaled;
  }

  if(accel.integer & ACCEL_VCENTER){
    y_target -= vcenter_offset_scaled - vh / 2; // is h always positible ? might need abs here
  }

  trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}


// does not set color
// no vertical centering
// used for fetures with its own vcenter settings (edges, aim_zone, window_center)
inline static void draw_positive_nvc(float x, float y, float w, float h)
{
  add_projection_x(&x, &w);

  float y_target = y - zero_gap_scaled / 2;

  if(predict){
    y_target -= predict == PREDICT_CROUCHJUMP ? predict_crouchjump_offset_scaled : predict_offset_scaled;
  }

  trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// does not set color
// automatic vertical centering
inline static void draw_negative(float x, float y, float w, float h)
{
  add_projection_x(&x, &w);

  float y_target;

  if(accel.integer & ACCEL_NEG_UP){
    y_target = ypos_scaled - zero_gap_scaled / 2 - h;
  }else{
    y_target = (y - h) + zero_gap_scaled / 2;
  }

  if(accel.integer & ACCEL_VCENTER){
    if(accel.integer & ACCEL_NEG_UP){
      y_target -= vcenter_offset_scaled - h / 2;
    }
    else
    {
      y_target += vcenter_offset_scaled - h / 2;
    }
  }
  
  trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

// does not set color
// custom vertical centering
inline static void draw_negative_vc(float x, float y, float w, float h, float vh)
{
  add_projection_x(&x, &w);

  float y_target;

  if(accel.integer & ACCEL_NEG_UP){
    y_target = ypos_scaled - zero_gap_scaled / 2 - (y - ypos_scaled);
  }else{
    y_target = (y - h) + zero_gap_scaled / 2;
  }

  if(accel.integer & ACCEL_VCENTER){
    if(accel.integer & ACCEL_NEG_UP){
      y_target -= vcenter_offset_scaled - vh / 2;
    }
    else
    {
      y_target += vcenter_offset_scaled - vh / 2;
    }
  }
  
  trap_R_DrawStretchPic(x, y_target, w, h, 0, 0, 0, 0, cgs.media.whiteShader);
}

//static void (*draw_polar)(const graph_bar * const, int, float, float);
inline static void _draw_vline(const graph_bar * const bar, int side, float y_offset, float vline_height)
{
  if(bar->polarity > 0)
  {
    set_color_inc_pred(accel_vline.integer & ACCEL_VL_USER_COLOR ? RGBA_I_VLINE : RGBA_I_POS);
    draw_positive(bar->x + (side == 1 ? bar->width - vline_size_scaled : 0), bar->y + y_offset, (bar->width > vline_size_scaled ? vline_size_scaled : bar->width / 2), vline_height);
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
static void PM_Friction(vec3_t velocity_io)
{
  // ignore slope movement
  float const speed = a.pml.walking ? VectorLength2(velocity_io) : VectorLength(velocity_io);

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
    a.pm.waterlevel <= 1 && a.pml.walking && !(a.pml.groundTrace.surfaceFlags & SURF_SLICK) &&
    !(a.pm_ps.pm_flags & PMF_TIME_KNOCKBACK))
  {
    float const control = speed < pm_stopspeed ? pm_stopspeed : speed;
    drop += control * pm_friction * pm_frametime;
  }

  // apply water friction even if just wading
  if (a.pm.waterlevel)
  {
    drop += speed * (a.pm_ps.pm_flags & PMF_PROMODE ? .5f : pm_waterfriction) * a.pm.waterlevel * pm_frametime;
  }

  // apply flying friction
  if (a.pm_ps.powerups[PW_FLIGHT])
  {
    drop += speed * pm_flightfriction * pm_frametime;
  }

  // this may cause bug in spectator mode
  if (a.pm_ps.pm_type == PM_SPECTATOR)
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
static void PM_Aircontrol(const vec3_t wishdir, vec3_t velocity_io) {
	float zspeed, speed, dot, k;
	int   i;

	// if (!pms.pm.cmd.rightmove || wishspeed == 0.0) {
	// 	return; 
	// }

	zspeed              = velocity_io[2];
	velocity_io[2] = 0;
	speed               = VectorNormalize(velocity_io);

	dot = DotProduct(velocity_io, wishdir);
	k   = 32.0f * 150.0f * dot * dot * pm_frametime;

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


// the function to calculate delta
// does not modify a.pm_ps.velocity (not sure anymore since its edited to be used in old version)
static float calc_accelspeed(const vec3_t wishdir, float const wishspeed, float const accel_, int verbose){
  int			i;
  float		addspeed, accelspeed, currentspeed;//, delta;
  vec3_t  velpredict;
  vec3_t velocity;

  (void)verbose;
  
  VectorCopy(a.pm_ps.velocity, velocity);

  Sys_SnapVector(velocity); // solves bug in spectator mode

  PM_Friction(velocity);

  currentspeed = DotProduct (velocity, wishdir); // the velocity speed part regardless the wish direction

  addspeed = wishspeed - currentspeed;

  if (addspeed <= 0) {
    return 0;
  }

  accelspeed = accel_*pm_frametime*wishspeed; // fixed pmove
  if (accelspeed > addspeed) {
      accelspeed = addspeed;
  }

  VectorCopy(velocity, velpredict);
    
  for (i=0 ; i<3 ; i++) {
    velpredict[i] += accelspeed*wishdir[i];
  }

  // add aircontrol to predict velocity vector
  if(move_type == MOVE_AIR_CPM && wishspeed && !a.pm.cmd.forwardmove && a.pm.cmd.rightmove) PM_Aircontrol(wishdir, velpredict);

  // clipping
  if((move_type == MOVE_WALK || move_type == MOVE_WALK_SLICK))
  {
    float speed = VectorLength(velpredict);

    PM_ClipVelocity(velpredict, a.pml.groundTrace.plane.normal,
        velpredict, OVERCLIP );
        
    VectorNormalize(velpredict);
    VectorScale(velpredict, speed, velpredict);
  }
  else if((move_type == MOVE_AIR || move_type == MOVE_AIR_CPM)
      && a.pml.groundPlane)
  {
    PM_ClipVelocity(velpredict, a.pml.groundTrace.plane.normal, velpredict, OVERCLIP );
  }

  // add snapping to predict velocity vector
  Sys_SnapVector(velpredict);
  
  return VectorLength(velpredict) - VectorLength(velocity);
}

// + anticlock, - clock
static void rotate_point_by_angle(vec_t vec[2], float rad){
  vec_t temp[2];
  temp[0] = vec[0];
  temp[1] = vec[1];

  vec[0] = cosf(rad) * temp[0] - sinf(rad) * temp[1];
  vec[1] = sinf(rad) * temp[0] + cosf(rad) * temp[1];
}

static int is_angle_within_bar(graph_bar *bar, float angle)
{
  
  return bar->angle > angle && bar->angle - (bar->width / resolution_ratio) * x_angle_ratio < angle;
}

/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  // these are allocation over and over again, we could save some cpu time by moving these to static 
  int i, j, i_color, negative_draw_skip, color_alternate_origin = 0;
  float y, dist_to_zero, dist_to_adj, height, line_height, speed_delta, center_angle = 0, angle_yaw_relative, normalizer, yaw_min_distance, yaw_distance;
  vec3_t wishdir_rotated;
  graph_bar *bar, *bar_adj, *bar_tmp, *window_bar = NULL, *center_bar = NULL;
  graph_bar *it, *start, *end, *start_origin, *end_origin; // end is included in loop

  
  // theoretical maximum is: addspeed * sin(45) * 2, but in practice its way less
  // hardcoded for now
  // inacurate approximation
  float norm_speed = VectorLength2(a.pm_ps.velocity);
  // dynamic normalizer breaks at 7000ups (value goes negative at this point), since this is a approximation, we can't make it bullet proof, hence this hotfix:
  if(norm_speed > 6000){
    norm_speed = 6000;
  }
  // there is side effect of the approximation and that is changing height profile based on speed, which falsefully give impression that accel is actually higher while it isn't
  // the trueness static boost for those who want real (accurate) height
  normalizer = (accel_trueness.integer & ACCEL_TN_STATIC_BOOST ? 2.56f * 1.41421356237f : -1*0.00025f*norm_speed+1.75f);
  if(move_type == MOVE_WALK || move_type == MOVE_WALK_SLICK){
    normalizer *= 15.f;// 38.4f * 1.41421356237f;
  }

  // recalculate the graph
  accel_graph_size = 0;
  // cur_speed_delta = 1000.f; // just reset to nonsence value // not needed it imo
  // for each horizontal pixel
  for(i = 0; i < resolution; ++i)
  {
    angle_yaw_relative = (i - (resolution / 2.f)) * -1 * x_angle_ratio;

    if(i == center){
      // the current (where the cursor points to)
        center_angle = angle_yaw_relative;
    }
    
    // rotating wishdir vector along whole x axis
    VectorCopy(wishdir, wishdir_rotated);
    rotate_point_by_angle(wishdir_rotated, angle_yaw_relative);

    // special case wishdir related values need to be recalculated (accel)
    if (move_type == MOVE_AIR_CPM && (!a.pm.cmd.rightmove || a.pm.cmd.forwardmove))
    {
      if(DotProduct(a.pm_ps.velocity, wishdir_rotated) < 0){
        speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, 2.5f, 0);
      }else{
        speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, pm_airaccelerate, 0);
      }
    }
    else
    {
      speed_delta = calc_accelspeed(wishdir_rotated, wishspeed, accel_, 0);
    }

    if(
      // automatically omit negative accel when plotting predictions, also when negatives are disabled ofc
      ((predict || accel_mode_neg.value == 0) && speed_delta <= 0)

      // when delta doesn't reach threshold value then the bar is completely ommited (currently works for predictions also, could be controlled by another cvar in the future)
      || fabs(speed_delta) < accel_threshold.value
    ){
      order++; // just control variable to distinct between adjecent bars (i hate it !)
      continue;
    }

    // grow the previous bar width when same speed_delta or within threshold
    if(accel_graph_size
      && accel_graph[accel_graph_size-1].value == speed_delta
      && accel_graph[accel_graph_size-1].order == order - 1 // since we are omitting sometimes this check is mandatory
    ){
      accel_graph[accel_graph_size-1].width += resolution_ratio;
    }
    else{
      bar = &(accel_graph[accel_graph_size]);
      bar->x = i * resolution_ratio;
      bar->polarity = speed_delta ? (speed_delta > 0 ? 1 : -1) : 0;
      bar->height = hud_height_scaled * (accel.integer & ACCEL_UNIFORM_VALUE ? bar->polarity : speed_delta / normalizer);
      if(accel_base_height.value > 0){
        bar->height += base_height_scaled * (bar->height > 0 ? 1 : -1); 
      }
      if(fabsf(bar->height) > max_height_scaled){
        bar->height = max_height_scaled * (bar->height > 0 ? 1 : -1);
      }
      bar->y = ypos_scaled + bar->height * -1; // * -1 because of y axis orientation
      bar->width = resolution_ratio;
      bar->value = speed_delta;
      bar->order = order++;
      bar->next = NULL;
      bar->angle = angle_yaw_relative; // (left > 0, right < 0)
      if(accel_graph_size){
        bar->prev = &(accel_graph[accel_graph_size-1]);
        accel_graph[accel_graph_size-1].next = bar;
      }else{
        bar->prev = NULL;
      }

      ++accel_graph_size;
    }
  }

  if(!accel_graph_size) return;

  // default
  start = &(accel_graph[0]);
  end = &(accel_graph[accel_graph_size-1]);

  // merge bars (not pretty at all)
  if(accel_merge_threshold.value && accel_graph_size >= 2)
  {
    int use_prev = 0;
    for(i = 0; i < accel_graph_size; ++i){
      bar = &(accel_graph[i]);
      if(bar->width <= accel_merge_threshold.value){
        if(bar->next && !bar->prev){
          bar->next->width += bar->width;
          bar->next->x = bar->x;
          start = bar->next;
          start->prev = NULL; // remove the edge bars
        }
        else if(!bar->next && bar->prev){
          bar->prev->width += bar->width;
          end = bar->prev;
          end->next = NULL; // remove the edge bars
          //prev_used = 1;
        }
        else if(bar->next && bar->prev) // in case of merge we are not guaranteed to have both prev/next so check it
        { 
          use_prev = (fabsf(bar->value - bar->prev->value) < fabsf(bar->value - bar->next->value));
          if(bar->polarity == bar->prev->polarity && (bar->polarity != bar->next->polarity || use_prev)){
            bar->prev->width += bar->width;
            //prev_used = 1;
          }
          else if(bar->polarity == bar->next->polarity && (bar->polarity != bar->prev->polarity || !use_prev)){
            bar->next->width += bar->width;
            bar->next->x = bar->x;
          } else {
            // both have opposite polarity -> do not merge
            continue;
          }

          bar->next->prev = bar->prev; // to skip current bar
          bar->prev->next = bar->next; // to skip current bar

          // fix order (ugly !)
          for(it = bar->next; it && it != end->next; it = it->next){
            it->order -= 1;
          }
        }
      }
    }
  }

  // determine the window bar
  int window_mode = ((!predict && accel.integer & ACCEL_WINDOW) || (predict && predict_window)) ? 1 : 0;
  int need_window = window_mode || accel.integer & ACCEL_COLOR_ALTERNATE || accel.integer & ACCEL_CUSTOM_WINDOW_COL || accel.integer & ACCEL_HL_WINDOW || accel_window_center.integer;
  yaw_min_distance = 2 * M_PI;
  if(need_window){
    for(it = start; it && it != end->next; it = it->next)
    {
      bar = it;  
      if(need_window && bar->polarity == 1){
        yaw_distance = fabsf(angle_short_radial_distance(vel_angle, yaw + bar->angle));
        if(yaw_min_distance > yaw_distance){
          yaw_min_distance = yaw_distance;
          window_bar = bar;
        }
      }
    }
  }

  // determine the center bar
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;
    if(is_angle_within_bar(bar, center_angle)){
      center_bar = bar;
      break;
    }
  }
  
  #if ACCEL_DEBUG
  if(accel_verbose.value && center_bar){
    trap_Print(vaf("center_bar->height: %.3f, center_bar->value: %.3f\n", center_bar->height, center_bar->value));
  }
  #endif

  // default after merge
  start_origin = start;
  end_origin = end;
  // from now on do not use index loop, iterate instead

  int hightlight_swap = 0;
  if(accel.integer & ACCEL_HL_G_ADJ
    && (
        // relevant moves -> VQ3 strafe or sidemove, CMP only strafe
        (!(a.pm_ps.pm_flags & PMF_PROMODE) && !a.pm.cmd.forwardmove && a.pm.cmd.rightmove)
        ||
        (a.pm.cmd.forwardmove && a.pm.cmd.rightmove)
    )
  ){
    hightlight_swap = a.pm.cmd.rightmove > 0 ? 1 : -1;
  } 
  
  // determine the start and end bars

  // when drawing just window bar skip all other positive (when we do not got window bar, draw full graph as normally)
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

  // start and end are at final stage lets get ordering right so we can alternate colors without color flickering

  if(accel.integer & ACCEL_COLOR_ALTERNATE){
    int count = 0;
    if(window_bar){
      for(it = window_bar; it; it = it->prev){
        count += 1;
      }
    }
    color_alternate_origin = color_alternate = !(count % 2); // try to keep same alternating colors 
  }


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

      window_parts_size = (window_bar->width - window_center_size_calculated) / 2;
    }
    // aim zone
    if(accel_aim_zone.integer){
      // relative / absolute size switch
      if(accel_aim_zone.integer & ACCEL_AIM_RELATIVE_SIZE){
        aim_zone_size_calculated = (accel_aim_zone_size.value / 100) * window_bar->width;
      }
      else{
        aim_zone_size_calculated = aim_zone_size_scaled;
      }

      // applying min size
      if(aim_zone_size_calculated < aim_zone_min_size_scaled){
        aim_zone_size_calculated = aim_zone_min_size_scaled;
      }

      // size check with actual window bar
      if(window_bar->width <= aim_zone_size_calculated)
      {
        aim_zone_size_calculated = window_bar->width;
      }
      
      if(angle_short_radial_distance(vel_angle, yaw + window_bar->angle) < 0){
        aim_zone_on_right_side = 1;
      }
    }
  }

  // *** draw ***
  // actual drawing si done here, for each bar in graph
  for(it = start; it && it != end->next; it = it->next)
  {
    bar = it;

    negative_draw_skip = 0;
    do // dummy loop
    {
      // bar's drawing
      if(bar->polarity > 0)  // positive bar
      {
        // set colors
        set_color_inc_pred(RGBA_I_POS);
        i_color = RGBA_I_BORDER;
        if(!predict){
          if(bar == window_bar && (accel.integer & ACCEL_CUSTOM_WINDOW_COL || accel.integer & ACCEL_HL_WINDOW))
          {
            if(center_bar && center_bar == window_bar && accel.integer & ACCEL_HL_WINDOW){
              set_color_inc_pred(RGBA_I_WINDOW_HL);
              i_color = RGBA_I_BORDER_WIDNOW_HL;
            } else {
              set_color_inc_pred(RGBA_I_WINDOW);
              i_color = RGBA_I_BORDER_WIDNOW;
            }
          } else if(accel.integer & ACCEL_HL_ACTIVE && center_bar && bar == center_bar){
            set_color_inc_pred(RGBA_I_HL_POS);
            i_color = RGBA_I_BORDER_HL_POS;
          }
          // is swap highlight active and the current bar is the swap-to once 
          else if(hightlight_swap && center_bar && (bar == center_bar->next || bar == center_bar->prev) // here used to be index +/- hightlight_swap instead we now use prev/next
            // and the value is greater then current
            && bar->value > center_bar->value
          ){
            set_color_inc_pred(RGBA_I_HL_G_ADJ);
            i_color = RGBA_I_BORDER_HL_G_ADJ;
          }
          else if(accel.integer & ACCEL_COLOR_ALTERNATE){
            if(color_alternate){
              set_color_inc_pred(RGBA_I_ALT);
              i_color = RGBA_I_BORDER_ALT;
            }else{
              set_color_inc_pred(RGBA_I_POS);
              i_color = RGBA_I_BORDER;
            }
            color_alternate = !color_alternate;
          }
        }
        
        // height = ypos_scaled-bar->y;
        height = bar->height;
      
        if(accel.integer & ACCEL_LINE_ACTIVE)
        {
          line_height = (height > line_size_scaled ? line_size_scaled : height); // (height - line_size_scaled)
          // if border does not cover whole area, draw rest of the bar
          if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
          {
            // draw uncovered area
            if(!predict && bar == window_bar){
              if(accel_window_center.integer){
                // in case we draw window center we need to split the window bar to parts
                if(window_parts_size > FLT_EPSILON * FLT_MIN)
                {
                  if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){ // so ugly TODO: refactor
                    if(aim_zone_size_calculated < window_parts_size){ // one part is cutted
                      if(aim_zone_on_right_side){
                        draw_positive_vc(bar->x, bar->y + line_height, window_parts_size, height - line_height, height);
                        draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size - aim_zone_size_calculated, height - line_height, height);
                      }
                      else
                      {
                        draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y + line_height, window_parts_size - aim_zone_size_calculated, height - line_height, height);
                        draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size, height - line_height, height);
                      }
                    }
                    else // one part is ommited
                    {
                      if(aim_zone_size_calculated > window_parts_size + window_center_size_calculated){ // the only part which we draw is cutted
                        float cut_size = aim_zone_size_calculated - (window_parts_size + window_center_size_calculated);
                        if(aim_zone_on_right_side){
                          // cutted left part
                          draw_positive_vc(bar->x, bar->y + line_height, window_parts_size - cut_size, height - line_height, height);
                        }else{
                          // cutted right part
                          draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated + cut_size, bar->y + line_height, window_parts_size - cut_size, height - line_height, height);
                        }
                      }
                      else
                      {
                        if(aim_zone_on_right_side){
                          // full left part
                          draw_positive_vc(bar->x, bar->y + line_height, window_parts_size, height - line_height, height);
                        }else{
                          // full right part
                          draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size, height - line_height, height);
                        }
                      }
                    }
                  }
                  else // only window center without substration of aim zone
                  {
                    draw_positive_vc(bar->x, bar->y + line_height, window_parts_size, height - line_height, height);
                    draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y + line_height, window_parts_size, height - line_height, height);
                  }
                }// else entire bar is handled as center so do not draw anything (window center drawing is done separately)
              }
              else // window center feature is disabled
              {
                if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){
                    if(aim_zone_on_right_side){
                      draw_positive_vc(bar->x, bar->y + line_height, bar->width - aim_zone_size_calculated, height - line_height, height);
                    }else{
                      draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y + line_height, bar->width - aim_zone_size_calculated, height - line_height, height);
                    }
                }
                else
                {
                  // just regular window/center bar no parts or cuts
                  draw_positive_vc(bar->x, bar->y + line_height, bar->width, height - line_height, height);
                }
              }
            }
            else // either predict or not window/center bar
            {
              draw_positive_vc(bar->x, bar->y + line_height, bar->width, height - line_height, height);
            }
          }
          // draw border line
          set_color_inc_pred(i_color);
          if(!predict && bar == window_bar){
            if(accel_window_center.integer){
              // in case we draw window center we need to split the window bar to parts
              if(window_parts_size > FLT_EPSILON * FLT_MIN)
              {
                if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){ // so ugly TODO: refactor
                  if(aim_zone_size_calculated < window_parts_size){ // one part is cutted
                    if(aim_zone_on_right_side){
                      draw_positive_vc(bar->x, bar->y, window_parts_size, line_height, height);
                      draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size - aim_zone_size_calculated, line_height, height);
                    }
                    else
                    {
                      draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y, window_parts_size - aim_zone_size_calculated, line_height, height);
                      draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, line_height, height);
                    }
                  }
                  else // one part is ommited
                  {
                    if(aim_zone_size_calculated > window_parts_size + window_center_size_calculated){ // the only part which we draw is cutted
                      float cut_size = aim_zone_size_calculated - (window_parts_size + window_center_size_calculated);
                      if(aim_zone_on_right_side){
                        // cutted left part
                        draw_positive_vc(bar->x, bar->y, window_parts_size - cut_size, line_height, height);
                      }else{
                        // cutted right part
                        draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated + cut_size, bar->y, window_parts_size - cut_size, line_height, height);
                      }
                    }
                    else
                    {
                      if(aim_zone_on_right_side){
                        // full left part
                        draw_positive_vc(bar->x, bar->y, window_parts_size, line_height, height);
                      }else{
                        // full right part
                        draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, line_height, height);
                      }
                    }
                  }
                }
                else // only window center without substration of aim zone
                {
                  draw_positive_vc(bar->x, bar->y, window_parts_size, line_height, height);
                  draw_positive_vc(bar->x + window_parts_size + window_center_size_calculated, bar->y, window_parts_size, line_height, height);
                }
              } // else entire bar is handled as center so do not draw anything (window center drawing is done separately)
            }
            else // window center feature is disabled
            {
              if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT && aim_zone_size_calculated > 0){
                  if(aim_zone_on_right_side){
                    draw_positive_vc(bar->x, bar->y, bar->width - aim_zone_size_calculated, line_height, height);
                  }else{
                    draw_positive_vc(bar->x + aim_zone_size_calculated, bar->y, bar->width + aim_zone_size_calculated, line_height, height);
                  }
              }
              else
              {
                // just regular window/center bar no parts or cuts
                draw_positive_vc(bar->x, bar->y, bar->width, line_height, height);
              }
            }
          }
          else // either predict or not window/center bar
          {
            draw_positive_vc(bar->x, bar->y, bar->width, line_height, height);
          }
        }
        else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA)){
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
        if(accel_mode_neg.value != ACCEL_NEG_MODE_ENABLED && accel_mode_neg.value != ACCEL_NEG_MODE_ADJECENT) continue;
        if((accel_mode_neg.value == ACCEL_NEG_MODE_ADJECENT)
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
          negative_draw_skip = 1;
          continue;
        }

        if(accel.integer & ACCEL_HL_ACTIVE && center_bar && bar == center_bar)
        {
          set_color_inc_pred(RGBA_I_HL_NEG);
          i_color = RGBA_I_BORDER_HL_NEG;
        }
        else{
          set_color_inc_pred(RGBA_I_NEG);
          i_color = RGBA_I_BORDER_NEG;
        }

        // height = (bar->y - ypos_scaled); // height should not be gapped imo need test // + zero_gap_scaled
        height = fabsf(bar->height);

        if(accel.integer & ACCEL_LINE_ACTIVE)
        {
          line_height = (height > line_size_scaled ? line_size_scaled : height);
          // if border does not cover whole area, draw rest of the bar
          if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
          {
            // draw uncovered area 
            draw_negative_vc(bar->x, bar->y - line_height, bar->width, height - line_height, height);
          }
          set_color_inc_pred(i_color);
          draw_negative_vc(bar->x, bar->y, bar->width, line_height, height);
        }
        else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA)){
          draw_negative(bar->x, bar->y, bar->width, height);
        }
      } // zero bars are separated
    } while(0);

    // vertical line's drawing
    if(!negative_draw_skip && bar->value && accel.integer & ACCEL_VL_ACTIVE)
    {
      dist_to_zero = fabs(bar->y - ypos_scaled);

      // for each side
      for(j = -1; j <= 1; j+= 2)
      {
        bar_adj = j == -1 ? bar->prev : bar->next;
        // check for adjecent and polarity
        if(!bar_adj || bar->polarity != bar_adj->polarity || bar->order + j != bar_adj->order){
          vline_to(bar, j, dist_to_zero);
          continue;
        }

        // we have adjecent bar and same polarity only case we need to calculate with the adjecent

        // skip vline for lower bar
        if((bar->polarity == 1 && bar->value < bar_adj->value)
              || (bar->polarity == -1 && bar->value > bar_adj->value)) continue;

        dist_to_adj = fabs(bar_adj->y - bar->y);
        vline_to(bar, j, dist_to_adj + (accel_vline.integer & ACCEL_VL_LINE_H ? fmin(dist_to_zero - dist_to_adj, line_size_scaled) : 0));
      }
    } // /vlines
  } // /for each bar

  // window center drawing (this one is specific to window bar so no need to have it in bar loop)
  if(!predict && window_bar){
    if(accel_window_center.integer){
      // would be easier to create actual or just helper bar and just pass it to the is_within_angle function
      if(accel_window_center.integer & ACCEL_WINDOW_CENTER_HL && window_bar->angle - (window_parts_size / resolution_ratio) * x_angle_ratio > center_angle && window_bar->angle - ((window_parts_size + window_center_size_calculated) / resolution_ratio) * x_angle_ratio < center_angle){
        set_color_inc_pred(RGBA_I_WINDOW_CENTER_HL);
        i_color = RGBA_I_BORDER_WINDOW_CENTER_HL;
      }
      else{
        set_color_inc_pred(RGBA_I_WINDOW_CENTER);
        i_color = RGBA_I_BORDER_WINDOW_CENTER;
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
        y_target = ypos_scaled - vcenter_offset_scaled - height / 2;
      }else{
        y_target = ypos_scaled - height;
      }

      if(accel_window_center_voffset.value > 0){
        y_target -= window_center_voffset_scaled;
      }

      if(accel.integer & ACCEL_LINE_ACTIVE)
      {
        line_height = (height > line_size_scaled ? line_size_scaled : height); // (height - line_size_scaled)
        // if border does not cover whole area, draw rest of the bar
        if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
        {
          // draw uncovered area
          if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT){
            if(aim_zone_size_calculated > window_parts_size && aim_zone_size_calculated < window_parts_size + window_center_size_calculated){
              float cut_size = aim_zone_size_calculated - window_parts_size;
              if(aim_zone_on_right_side){
                draw_positive_nvc(window_bar->x + window_parts_size, y_target + line_height, window_center_size_calculated - cut_size, height - line_height);
              }
              else
              {
                draw_positive_nvc(window_bar->x + window_parts_size + cut_size, y_target + line_height, window_center_size_calculated - cut_size, height - line_height);
              }
            } else if(aim_zone_size_calculated < window_parts_size){
              // no cutting needed
              draw_positive_nvc(window_bar->x + window_parts_size, y_target + line_height, window_center_size_calculated, height - line_height);
            } // else whole center is cutted off
          }
          else // no subtraction of aim zone
          {
            draw_positive_nvc(window_bar->x + window_parts_size, y_target + line_height, window_center_size_calculated, height - line_height);
          }
        }
        // draw border line
        set_color_inc_pred(i_color);
        if(accel_aim_zone.integer & ACCEL_AIM_SUBTRACT){
          if(aim_zone_size_calculated > window_parts_size && aim_zone_size_calculated < window_parts_size + window_center_size_calculated){
            float cut_size = aim_zone_size_calculated - window_parts_size;
            if(aim_zone_on_right_side){
              draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated - cut_size, line_height);
            }
            else
            {
              draw_positive_nvc(window_bar->x + window_parts_size + cut_size, y_target, window_center_size_calculated - cut_size, line_height);
            }
          } else if(aim_zone_size_calculated < window_parts_size){
            // no cutting needed
            draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated, line_height);
          } // else whole center is cutted off
        }
        else // no subtraction of aim zone
        {
          draw_positive_nvc(window_bar->x + window_parts_size, y_target, window_center_size_calculated, line_height);
        }
      }
      else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA)){
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
      if(accel_aim_zone.integer & ACCEL_AIM_HL && window_bar->angle - (aim_zone_offset / resolution_ratio) * x_angle_ratio > center_angle && window_bar->angle - ((aim_zone_offset + aim_zone_size_calculated) / resolution_ratio) * x_angle_ratio < center_angle){
        set_color_inc_pred(RGBA_I_AIM_ZONE_HL);
        i_color = RGBA_I_BORDER_AIM_ZONE_HL;
      }
      else{
        set_color_inc_pred(RGBA_I_AIM_ZONE);
        i_color = RGBA_I_BORDER_AIM_ZONE;
      }

      const float x = window_bar->x + aim_zone_offset;

      height = window_bar->height;

      if(accel_aim_zone_height.value > 0){
        height = accel_aim_zone.integer & ACCEL_AIM_RELATIVE_HEIGHT ? (accel_aim_zone_height.value / 100) * height : aim_zone_height_scaled;
      }

      if(accel_aim_zone_min_height.value > 0 && height < aim_zone_min_height_scaled){
        height = aim_zone_min_height_scaled;
      }

      float y_target;
      if(((accel_aim_zone.integer & ACCEL_AIM_VCENTER_FORCE) && (accel_aim_zone.integer & ACCEL_AIM_VCENTER))
        || (!(accel_aim_zone.integer & ACCEL_AIM_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
      ){
        y_target = ypos_scaled - vcenter_offset_scaled - height / 2;
      }
      else
      {
        y_target = ypos_scaled - height;
      }

      if(accel_aim_zone_voffset.value > 0){
        y_target -= aim_zone_voffset_scaled;
      }

      // if(accel_aim_zone_height.value > 0){
      //   draw_positive_nvc(x, ypos_scaled - aim_zone_height_scaled - (accel_aim_zone.integer & ACCEL_AIM_VCENTER ? vcenter_offset_scaled - aim_zone_height_scaled / 2 : 0), aim_zone_size_calculated, aim_zone_height_scaled);
      // }
      // else
      if(accel.integer & ACCEL_LINE_ACTIVE)
      {
        line_height = (height > line_size_scaled ? line_size_scaled : height);
        // if border does not cover whole area, draw rest of the bar
        if(height > line_height && !(accel.integer & ACCEL_DISABLE_BAR_AREA))
        {
          // draw uncovered area
          draw_positive_nvc(x, y_target + line_height, aim_zone_size_calculated, height - line_height);
        }
        // draw border line
        set_color_inc_pred(i_color);
        draw_positive_nvc(x, y_target, aim_zone_size_calculated, line_height);
      }
      else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA)){
        draw_positive_nvc(x, y_target, aim_zone_size_calculated, height);
      }
    }
  }

  // condensed / zero bars
  if(!predict)
  {
    if(accel.integer & ACCEL_CB_ACTIVE)
    {
      color_alternate = color_alternate_origin;
      for(it = start_origin; it && it != end_origin->next; it = it->next)
      {
        bar = it;
        if(bar->polarity > 0){
          if(accel.integer & ACCEL_HL_ACTIVE && center_bar && bar == center_bar){
            i_color = RGBA_I_HL_POS;
          }
          else if(accel.integer & ACCEL_COLOR_ALTERNATE)
          {
            if(color_alternate){
              i_color = RGBA_I_ALT;
            }else{
              i_color = RGBA_I_POS;
            }
            color_alternate = !color_alternate;
          }
          else {
            i_color = RGBA_I_POS;
          }
        }
        else if(bar->polarity < 0){
          if(accel.integer & ACCEL_HL_ACTIVE && center_bar && bar == center_bar){
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
    // / condensed / zero bars

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

        if(angle_short_radial_distance(vel_angle, yaw + bar->angle) < 0){
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
            if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
            {
              lh = bar->height * (accel_edge_height.value / 100);
              rh = bar_tmp->height * (accel_edge_height.value / 100);
            }
            else if(accel.integer & ACCEL_LINE_ACTIVE)
            {
              lh = (bar->height > line_size_scaled ? line_size_scaled : bar->height) * (accel_edge_height.value / 100);
              rh = (bar_tmp->height > line_size_scaled ? line_size_scaled : bar_tmp->height) * (accel_edge_height.value / 100);
            }
          }
          else
          {
            rh = lh = edge_height_scaled;
          }
        }
        else if(!(accel.integer & ACCEL_DISABLE_BAR_AREA))
        {
          lh = bar->height;
          rh = bar_tmp->height;
        }
        else if(accel.integer & ACCEL_LINE_ACTIVE)
        {
          lh = bar->height > line_size_scaled ? line_size_scaled : bar->height;
          rh = bar_tmp->height > line_size_scaled ? line_size_scaled : bar_tmp->height;
        }
        else // no need to draw anything in the rest of the cases
        {
          // skip all positive we just handled
          it = bar_tmp;
          continue;
        }

        // apply min height // before FULL_SIZE ? 
        if(accel_edge_min_height.value > 0){
          lh = lh < edge_min_height_scaled ? edge_min_height_scaled : lh;
          rh = rh < edge_min_height_scaled ? edge_min_height_scaled : rh;
        }

        float ly = ypos_scaled - lh,
              ry = ypos_scaled - rh;

        if(accel_edge.integer & ACCEL_EDGE_FULL_SIZE){
          lh = lh * 2 + zero_gap_scaled; 
          rh = rh * 2 + zero_gap_scaled;
        }

        if(((accel_edge.integer & ACCEL_EDGE_VCENTER_FORCE) && (accel_edge.integer & ACCEL_EDGE_VCENTER))
          || (!(accel_edge.integer & ACCEL_EDGE_VCENTER_FORCE) && (accel.integer & ACCEL_VCENTER))
        ){
          ly = ypos_scaled - vcenter_offset_scaled - lh / 2;
          ry = ypos_scaled - vcenter_offset_scaled - rh / 2;
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
        trap_R_SetColor(a.graph_rgba[i_color]);
        draw_positive_nvc(lx, ly, lw, lh);

        // right
        trap_R_SetColor(a.graph_rgba[right_i_color]);
        draw_positive_nvc(rx, ry, rw, rh);

        // skip all positive we just handled
        it = bar_tmp;
      }
    }// /edges

    // point line
    if(accel.integer & ACCEL_PL_ACTIVE && center_bar)
    {
      set_color_inc_pred(RGBA_I_POINT);

      y = ypos_scaled + hud_height_scaled * (center_bar->value / normalizer) * -1;
      if(center_bar->value > 0){
        draw_positive(center - (accel_point_line_size.value * cgs.screenXScale) / 2, y, accel_point_line_size.value * cgs.screenXScale, ypos_scaled - y);
      }
      else if(center_bar->value < 0){
        draw_negative(center - (accel_point_line_size.value * cgs.screenXScale) / 2, y, accel_point_line_size.value * cgs.screenXScale, y - ypos_scaled);
      }
    } // /point line
  }

  trap_R_SetColor(NULL);
}

static void PM_SlickAccelerate(const vec3_t wishdir, float const wishspeed, float const accel_)
{
  PM_Accelerate(wishdir, wishspeed, accel_);
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
  if (a.pm_ps.pm_flags & PMF_PROMODE && accel_trueness.integer & ACCEL_TN_CPM) //  && (!pms.pm.cmd.forwardmove && pms.pm.cmd.rightmove) => there is also forward move
  {
    move_type = MOVE_AIR_CPM;
    PM_Accelerate(wishdir,
        (!a.pm.cmd.forwardmove && a.pm.cmd.rightmove && wishspeed > cpm_airwishspeed ?
        cpm_airwishspeed : wishspeed),
        (!a.pm.cmd.forwardmove && a.pm.cmd.rightmove ? cpm_airstrafeaccelerate :
        (DotProduct(a.pm_ps.velocity, wishdir) < 0 ? 2.5f : pm_airaccelerate)));
  }
  else
  {
    move_type = MOVE_AIR;
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate);
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

  if ( a.pm.waterlevel > 2 && DotProduct( a.pml.forward, a.pml.groundTrace.plane.normal ) > 0 ) {
    // begin swimming
    // PAL_WaterMove();
    return;
  }

  if (PM_CheckJump(&a.pm, &a.pm_ps, &a.pml)) {
    // jumped away
    if ( a.pm.waterlevel > 1 ) {
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
  a.pml.forward[2] = 0;
  a.pml.right[2] = 0;

  // project the forward and right directions onto the ground plane
  PM_ClipVelocity (a.pml.forward, a.pml.groundTrace.plane.normal, a.pml.forward, OVERCLIP );
  PM_ClipVelocity (a.pml.right, a.pml.groundTrace.plane.normal, a.pml.right, OVERCLIP );
  //
  VectorNormalize (a.pml.forward); // aprox. 1 unit in space facing forward based on cameraview
  VectorNormalize (a.pml.right); // aprox. 1 unit in space facing right based on cameraview

  for ( i = 0 ; i < 3 ; i++ ) {
    wishvel[i] = a.pml.forward[i]*a.pm.cmd.forwardmove + a.pml.right[i]*a.pm.cmd.rightmove; // added fractions of direction (the camera once) increased over move (127 run speed)
  }
  // when going up or down slopes the wish velocity should Not be zero // but its value doesnt come from anywhere here so wtf...
  //wishvel[2] = 0;

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
  if(a.pm.waterlevel)
  {
    float	waterScale = a.pm.waterlevel / 3.0f;
    if(a.pm_ps.pm_flags & PMF_PROMODE)
    {
      waterScale = 1.0 - (1.0 - (a.pm.waterlevel == 1 ? 0.585 : 0.54)) * waterScale;
    }
    else {
      waterScale = 1.0f - ( 1.0f - pm_swimScale ) * waterScale;
    }

    if ( wishspeed > a.pm_ps.speed * waterScale ) {
      wishspeed = a.pm_ps.speed * waterScale;
    }
  }

  // when a player gets hit, they temporarily lose
  // full control, which allows them to be moved a bit
  move_type = MOVE_WALK;
  if (accel_trueness.integer & ACCEL_TN_GROUND)
  {
    if (a.pml.groundTrace.surfaceFlags & SURF_SLICK || a.pm_ps.pm_flags & PMF_TIME_KNOCKBACK)
    {
      move_type = MOVE_WALK_SLICK;
      PM_SlickAccelerate(wishdir, wishspeed, a.pm_ps.pm_flags & PMF_PROMODE ? cpm_slickaccelerate : pm_slickaccelerate);
    }
    else
    {
      // don't reset the z velocity for slopes
      // a.pm_ps.velocity[2] = 0;
      PM_Accelerate(wishdir, wishspeed, a.pm_ps.pm_flags & PMF_PROMODE ? cpm_accelerate : pm_accelerate);
    }
  }
  else
  {
    PM_Accelerate(wishdir, wishspeed, pm_airaccelerate);
  }
}
