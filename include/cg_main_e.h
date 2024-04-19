#ifndef CG_MAIN_E_H
#define CG_MAIN_E_H

#include "q_shared.h"

#if __APPLE__
void init_hud(void);
void update_hud(void);
void draw_hud(void);
qboolean trap_GetUserCmd(int32_t, usercmd_t*);
#else
void __wrap_init_hud(void);
void __wrap_update_hud(void);
void __wrap_draw_hud(void);
qboolean __wrap_trap_GetUserCmd(int32_t, usercmd_t*);
#endif

#endif //CG_MAIN_E_H
