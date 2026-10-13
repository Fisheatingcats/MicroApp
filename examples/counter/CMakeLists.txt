cmake_minimum_required(VERSION 3.22)
project(MicroAppCounter LANGUAGES C ASM)
add_subdirectory(../.. microapp)
microapp_add_arm_app(counter SOURCES counter.c helper.c HOST_HEADERS host_api.h
    PROFILE ${CMAKE_CURRENT_SOURCE_DIR}/target.json MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/app.json)
