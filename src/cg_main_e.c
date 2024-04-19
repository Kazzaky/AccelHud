#include "cg_main_e.h"
#include "cg_accel.h"
#include "cg_cursor.h"
#include "cg_cvar.h"
#include "cg_local.h"
#include "cg_utils.h"

#include "compass.h"
#include "cg_cgaz.h"
#include "cg_snap.h"
#include "pitch.h"
#include "cg_ammo.h"
#include "cg_jump.h"
#include "cg_timer.h"
#include "q_shared.h"
#include <stdlib.h>


extern void __real_init_hud(void);
extern void __real_update_hud(void);
extern void __real_draw_hud(void);
extern qboolean __real_trap_GetUserCmd(int32_t, usercmd_t*);

static vmCvar_t flickfree;

static vmCvar_t accel_draw_order;
static vmCvar_t compass_draw_order;
static vmCvar_t cgaz_draw_order;
static vmCvar_t snap_draw_order;
static vmCvar_t pitch_draw_order;
static vmCvar_t ammo_draw_order;
static vmCvar_t jump_draw_order;
static vmCvar_t timer_draw_order;
static vmCvar_t cursor_draw_order;

static cvarTable_t draw_order_cvars[] = {
  { &accel_draw_order, "p_accel_draw_order", "0", CVAR_ARCHIVE_ND },
  { &compass_draw_order, "p_compass_draw_order", "0", CVAR_ARCHIVE_ND },
  { &cgaz_draw_order, "p_cgaz_draw_order", "0", CVAR_ARCHIVE_ND },
  { &snap_draw_order, "p_snap_draw_order", "0", CVAR_ARCHIVE_ND },
  { &pitch_draw_order, "p_pitch_draw_order", "0", CVAR_ARCHIVE_ND },
  { &ammo_draw_order, "p_ammo_draw_order", "0", CVAR_ARCHIVE_ND },
  { &jump_draw_order, "p_jump_draw_order", "0", CVAR_ARCHIVE_ND },
  { &timer_draw_order, "p_timer_draw_order", "0", CVAR_ARCHIVE_ND },
  { &cursor_draw_order, "p_cursor_draw_order", "0", CVAR_ARCHIVE_ND },
};

typedef struct {
    vmCvar_t * cvar;
    void (*draw_ptr)(void);
} drawCvarPair_t;

static drawCvarPair_t draw_order_table[] = {
    { &accel_draw_order, draw_accel },
    { &compass_draw_order, draw_compass },
    { &cgaz_draw_order, draw_cgaz },
    { &snap_draw_order, draw_snap },
    { &pitch_draw_order, draw_pitch },
    { &ammo_draw_order, draw_ammo },
    { &jump_draw_order, draw_jump },
    { &timer_draw_order, draw_timer },
    { &cursor_draw_order, draw_cursor },
};

typedef struct {
    void (*draw_ptr)(void);
    size_t order;
} drawOrderPair_t;

static int cmp_draw_pair(const void * v1, const void * v2)
{
    const drawOrderPair_t * p1 = (drawOrderPair_t *)v1;
    const drawOrderPair_t * p2 = (drawOrderPair_t *)v2;
    
    if(p1->order < p2->order){
        return -1;
    } else if(p1->order > p2->order){
        return 1;
    }

    return 0;
}

static int last_modification_count = -1;

static size_t i, j, k;
static void (*default_draw_order[])(void) = {
    draw_compass,
    draw_cgaz,
    draw_snap,
    draw_pitch,
    draw_ammo,
    draw_jump,
    draw_timer,
    draw_accel,
    draw_cursor,
};

static void (*draw_call_list[ARRAY_LEN(default_draw_order)])(void);


#if __APPLE__
void init_hud(void)
#else
void __wrap_init_hud(void)
#endif
{
    trap_Cvar_Register(&flickfree, "p_flickfree", "1", CVAR_ARCHIVE_ND);
    init_cvars(draw_order_cvars, ARRAY_LEN(draw_order_cvars));
    __real_init_hud();
    init_accel();
    init_cursor();

    for(i = 0; i < (int)ARRAY_LEN(draw_order_table); ++i){
        last_modification_count += draw_order_table[i].cvar->modificationCount;
    }
}

#if __APPLE__
void update_hud(void)
#else
void __wrap_update_hud(void)
#endif
{
    trap_Cvar_Update(&flickfree);
    flickfree.integer = cvar_getInteger("p_flickfree");

    update_cvars(draw_order_cvars, ARRAY_LEN(draw_order_cvars));
    __real_update_hud();

    if(!cvar_getInteger("mdd_hud")) return;

    update_accel();
    update_cursor();

    size_t len = ARRAY_LEN(draw_order_table);

    int modification_count = 0;
    for(i = 0; i < len; ++i){
        modification_count += draw_order_table[i].cvar->modificationCount;
    }

    // if nothing changed we can use old draw call list
    if(modification_count == last_modification_count) return;

    // else rebuild draw call list
    //trap_Print("Rebuilding call order list");

    // clear draw call list in case there is wrong cvar value
    for(i = 0; i < len; ++i)
    {
        draw_call_list[i] = NULL;
    }

    drawOrderPair_t tmp[len];
    size_t tmp_count = 0;

    int draw_call_index = 0;

    // loop through each order value
    for(i = 0; i < len; ++i)
    {
        tmp_count = 0;

        for(j = 0; j < len; ++j){
            // if there is cvar with current order value, save it to tmp table
            if(draw_order_table[j].cvar->integer == (int)i){
                tmp[tmp_count++].draw_ptr = draw_order_table[j].draw_ptr;
            }
        }

        // find default order value for draw function
        for(j = 0; j < tmp_count; ++j){
            for(k = 0; k < ARRAY_LEN(default_draw_order); ++k){
                if(tmp[j].draw_ptr == default_draw_order[k]){
                    tmp[j].order = k;
                    break;
                }
            }
            // there is allways a match, no need to check or use ilogical value (assert would be nice tho)
        }

        qsort(tmp, tmp_count, sizeof(drawOrderPair_t), cmp_draw_pair);

        // store ordered table
        for(j = 0; j < tmp_count; ++j){
            // no need to check draw_calls_index, it will never exceed size
            draw_call_list[draw_call_index++] = tmp[j].draw_ptr;
        }
    }

    // save modification count so we don't have to calculate order every update
    last_modification_count = modification_count;
}

#if __APPLE__
void draw_hud(void)
#else
void __wrap_draw_hud(void)
#endif
{
    if(!trap_CM_NumInlineModels()) return;

    if(!cvar_getInteger("mdd_hud")) return;

    for(i = 0; i < ARRAY_LEN(draw_call_list); ++i){
        if(draw_call_list[i]) draw_call_list[i]();
    }

    // __real_draw_hud(); // original draw_hud function is no longer used
    // draw_accel();
}

#if __APPLE__
qboolean trap_GetUserCmd(int32_t cmdNumber, usercmd_t* ucmd)
#else
qboolean __wrap_trap_GetUserCmd(int32_t cmdNumber, usercmd_t* ucmd)
#endif
{
    qboolean res = __real_trap_GetUserCmd(cmdNumber, ucmd);

    if(flickfree.integer && res)
    {
        int8_t const scale = getPs()->stats[13] & PSF_USERINPUT_WALK ? 64 : 127;
        ucmd->forwardmove = ucmd->forwardmove < 0 ? -scale : (ucmd->forwardmove > 0 ? scale : 0);
        ucmd->rightmove = ucmd->rightmove < 0 ? -scale : (ucmd->rightmove > 0 ? scale : 0);
        ucmd->upmove = ucmd->upmove < 0 ? -scale : (ucmd->upmove > 0 ? scale : 0);
    }

    return res;
}

