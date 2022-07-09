#ifndef CG_MAIN_E_H
#define CG_MAIN_E_H

#if __APPLE__
void init_hud(void);
void update_hud(void);
void draw_hud(void);
#else
void __wrap_init_hud(void);
void __wrap_update_hud(void);
void __wrap_draw_hud(void);
#endif

#endif //CG_MAIN_E_H
