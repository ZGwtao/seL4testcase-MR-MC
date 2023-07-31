#
# Copyright 2023, zhuguangtao@iie.ac.cn, SKLOIS
#
cmake_minimum_required(VERSION 3.7.2)

project(sel4testcase-mce C)

set(configure_string "")
add_config_library(sel4testcase-mce "${configure_string}")

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)
find_package(sel4_projects_libs REQUIRED)

set(UserLinkerGCSections OFF CACHE BOOL "" FORCE)

musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

set(LibNanopb ON CACHE BOOL "" FORCE)

sel4_projects_libs_import_libraries()

file(GLOB static
  mce.c
)
add_executable(sel4testcase-mce EXCLUDE_FROM_ALL ${static})

target_include_directories(sel4testcase-mce PRIVATE "include")
target_link_libraries(
    sel4testcase-mce
    PUBLIC sel4_autoconf muslc sel4 sel4runtime sel4allocman sel4vka
           sel4utils sel4test sel4platsupport sel4muslcsys
    PRIVATE sel4testcase-mce_Config
)
target_compile_options(sel4testcase-mce PRIVATE -Werror -g)

include(rootserver)
DeclareRootserver(sel4testcase-mce)