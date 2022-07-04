set(CMAKE_VERBOSE_MAKEFILE ON)

target_sources(cgame_obj PRIVATE cg_accel.c cg_main_e.c common.c) #cg_syscall_e.c
target_include_directories(cgame_obj PUBLIC ../include)
target_include_directories(cgame_obj PRIVATE ../cgame_proxymod/src)

target_link_options(${BINARY_NAME} PRIVATE "LINKER:--wrap,init_hud,--wrap,update_hud,--wrap,draw_hud")
target_link_options(UnitTest PRIVATE "LINKER:--wrap,init_hud,--wrap,update_hud,--wrap,draw_hud")
