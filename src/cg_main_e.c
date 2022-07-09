#include "cg_main_e.h"
#include "cg_accel.h"

extern void __real_init_hud(void);
extern void __real_update_hud(void);
extern void __real_draw_hud(void);

#if __APPLE__
void init_hud(void)
#else
void __wrap_init_hud(void)
#endif
{
    __real_init_hud();
    init_accel();
}

#if __APPLE__
void update_hud(void)
#else
void __wrap_update_hud(void)
#endif
{
    __real_update_hud();
    update_accel();
}

#if __APPLE__
void draw_hud(void)
#else
void __wrap_draw_hud(void)
#endif
{
    __real_draw_hud();
    draw_accel();
}
