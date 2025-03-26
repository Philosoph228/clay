cmake_minimum_required(VERSION 3.27)
project(win32_gdi C)

set(CMAKE_C_STANDARD 99)

add_executable(win32_gdi WIN32 main.c)

target_compile_options(win32_gdi PUBLIC)
target_include_directories(win32_gdi PUBLIC .)

if(USE_MSIMG32)
    target_compile_definitions(win32_gdi PUBLIC USE_MSIMG32)
    target_link_libraries(win32_gdi PUBLIC msimg32)
endif()

add_custom_command(
        TARGET win32_gdi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        ${CMAKE_CURRENT_BINARY_DIR}/resources)
