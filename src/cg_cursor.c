/*
 * Author: Palme
 * Licence: GPLv3
 *
 * Code heavily use cgame_proxymode and Quake III Arena code,
 * additional licence rules may apply.
 */

#include "cg_cursor.h"

#include "cg_cvar.h"
#include "cg_local.h"
#include "q_shared.h"

#define MAX_RESOLUTION 3840 // 4K hardcoded

static vmCvar_t cursor;
static vmCvar_t cursor_line_size;
static vmCvar_t cursor_yhw;
static vmCvar_t cursor_custom_tga;
static vmCvar_t cursor_offset;
static vmCvar_t cursor_rgba;
static vmCvar_t cursor_test;
  
static cvarTable_t cursor_cvars[] = {
  { &cursor, "p_cursor", "0b0000", CVAR_ARCHIVE_ND },

#define CURSOR_ENABLE       1
#define CURSOR_POS_ACCEL    2 // double the height and vertical center
#define CURSOR_TOPBOTTOM    4
#define CURSOR_CUSTOM       8

  { &cursor_line_size, "p_cursor_line_size", "3", CVAR_ARCHIVE_ND },
  { &cursor_yhw, "p_cursor_yhw", "180 30 1", CVAR_ARCHIVE_ND },
  { &cursor_custom_tga, "p_cursor_custom_tga", "", CVAR_ARCHIVE_ND },
  { &cursor_offset, "p_cursor_offset", "0 0", CVAR_ARCHIVE_ND },

  { &cursor_rgba, "p_cursor_rgba", "1 1 1 .8", CVAR_ARCHIVE_ND },
  { &cursor_test, "p_cursor_test", "0 0 1 1", CVAR_ARCHIVE_ND },
};

typedef struct
{
  vec3_t        yhw;
  vec4_t        rgba;
  vec2_t        offset;
  int           custom_mod_count;
  qhandle_t     custom;
} cursor_t;

static cursor_t c;


void init_cursor(void)
{
  init_cvars(cursor_cvars, ARRAY_LEN(cursor_cvars));
  c.custom_mod_count = -1;
}

void update_cursor(void)
{
  update_cvars(cursor_cvars, ARRAY_LEN(cursor_cvars));
  cursor.integer           = cvar_getInteger("p_cursor"); // this is only relevant to be updated before draw
}

// just so we do not allocate memory each time
static int resolution, center;
static float x_angle_ratio, ypos_scaled, hud_height_scaled, cursor_gap_scaled, cursor_line_size_scaled;
static float y, h;
static vec2_t offset_scaled;

void draw_cursor(void)
{
  if (!cursor.integer) return;

  // update cvar values
  ParseVec(cursor_yhw.string, c.yhw, 3);
  ParseVec(cursor_rgba.string, c.rgba, 4);
  ParseVec(cursor_offset.string, c.offset, 2);

  vec4_t draw_test;
  ParseVec(cursor_test.string, draw_test, 4);

  resolution = MAX_RESOLUTION < cgs.glconfig.vidWidth ? MAX_RESOLUTION : cgs.glconfig.vidWidth; // could cause bugs at >4k
  center = resolution / 2;
  x_angle_ratio = cg.refdef.fov_x / resolution;
  ypos_scaled = c.yhw[0] * cgs.screenXScale;
  hud_height_scaled = c.yhw[1] * cgs.screenXScale; // X scale everywhere for consistency ?
  cursor_gap_scaled = c.yhw[2] * cgs.screenXScale;
  cursor_line_size_scaled = cursor_line_size.value * cgs.screenXScale;
  offset_scaled[0] = c.offset[0] * cgs.screenXScale;
  offset_scaled[1] = c.offset[1] * cgs.screenXScale;

  // cursor drawing
  if(cursor.integer & CURSOR_CUSTOM)
  {
    if(cursor_custom_tga.modificationCount > c.custom_mod_count){
      c.custom  = trap_R_RegisterShader(cursor_custom_tga.string);
    }
    trap_R_DrawStretchPic(center + offset_scaled[0], ypos_scaled + offset_scaled[1], cursor_gap_scaled, hud_height_scaled, draw_test[0], draw_test[1], draw_test[2], draw_test[3], c.custom);
  } else {
    trap_R_SetColor(c.rgba);
    
    if(cursor.integer & CURSOR_POS_ACCEL){
      y = ypos_scaled - hud_height_scaled + offset_scaled[1];
      h = hud_height_scaled * 2;
    } else {
      y = ypos_scaled + offset_scaled[1];
      h = hud_height_scaled;
    }

    if(cursor.integer & CURSOR_TOPBOTTOM){
      // top line
      trap_R_DrawStretchPic(center - (cursor_gap_scaled / 2) - cursor_line_size_scaled + offset_scaled[0], y + h, cursor_line_size_scaled * 2 + cursor_gap_scaled, cursor_line_size_scaled, 0, 0, 0, 0, cgs.media.whiteShader);
      // bottom line
      trap_R_DrawStretchPic(center - (cursor_gap_scaled / 2) - cursor_line_size_scaled + offset_scaled[0], y - cursor_line_size_scaled, cursor_line_size_scaled * 2 + cursor_gap_scaled, cursor_line_size_scaled, 0, 0, 0, 0, cgs.media.whiteShader);
    }
    // right line
    trap_R_DrawStretchPic(center + (cursor_gap_scaled / 2) + offset_scaled[0], y, cursor_line_size_scaled, h, 0, 0, 0, 0, cgs.media.whiteShader);
    // left line
    trap_R_DrawStretchPic(center - (cursor_gap_scaled / 2) - cursor_line_size_scaled + offset_scaled[0], y, cursor_line_size_scaled, h, 0, 0, 0, 0, cgs.media.whiteShader);

    trap_R_SetColor(NULL); // reset
  }
}
