#include "cg_main_e.h"
#include "cg_accel.h"

extern void __real_init_hud(void);
extern void __real_update_hud(void);
extern void __real_draw_hud(void);

void __wrap_init_hud(void)
{
    __real_init_hud();
    init_accel();
}

void __wrap_update_hud(void)
{
    __real_update_hud();
    update_accel();
}

void __wrap_draw_hud(void)
{
    __real_draw_hud();
    draw_accel();
}
