
target_sources(cgame_obj PRIVATE cg_cursor.c cg_accel.c cg_hud_e.c common.c)
target_include_directories(cgame_obj PUBLIC ../include)
target_include_directories(cgame_obj PRIVATE ../cgame_proxymod/src)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    if(NOT OBJCOPY_CMD)
        set(OBJCOPY_CMD objcopy)
    endif()

    add_custom_command(OUTPUT
        ${CMAKE_BINARY_DIR}/cg_hud.c.o
        COMMAND ${OBJCOPY_CMD} --redefine-sym _init_hud=___real_init_hud --redefine-sym _update_hud=___real_update_hud --redefine-sym _draw_hud=___real_draw_hud --redefine-sym _del_hud=___real_del_hud ../cgame_proxymod/src/CMakeFiles/cgame_obj.dir/cg_hud.c.o
        DEPENDS cgame_obj
        VERBATIM
    )

    add_custom_command(OUTPUT
        ${CMAKE_BINARY_DIR}/cg_syscall.c.o
        COMMAND ${OBJCOPY_CMD} --redefine-sym _trap_GetUserCmd=___real_trap_GetUserCmd ../cgame_proxymod/src/CMakeFiles/cgame_obj.dir/cg_syscall.c.o
        DEPENDS cgame_obj
        VERBATIM
    )

    add_custom_target(cgame_obj_mod ALL
        DEPENDS ${CMAKE_BINARY_DIR}/cg_hud.c.o ${CMAKE_BINARY_DIR}/cg_syscall.c.o
    )

    add_dependencies(${BINARY_NAME} cgame_obj_mod)
else()
    target_link_options(${BINARY_NAME} PRIVATE "LINKER:--wrap,init_hud,--wrap,update_hud,--wrap,draw_hud,--wrap,trap_GetUserCmd,--wrap,del_hud")
    target_link_options(UnitTest PRIVATE "LINKER:--wrap,init_hud,--wrap,update_hud,--wrap,draw_hud,--wrap,trap_GetUserCmd,--wrap,del_hud")
endif()
