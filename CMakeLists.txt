cmake_minimum_required(VERSION 3.16)

project(AccelHud VERSION 0.3.0)

set(ACCEL_VERSION ${PROJECT_VERSION})
configure_file(accel_version.h.in accel_version.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(cgame_proxymod)
add_subdirectory(src)
