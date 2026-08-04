cmake_minimum_required(VERSION 3.25)
project(libvterm C)

file(GLOB VTERM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(vterm STATIC ${VTERM_SOURCES})

target_include_directories(vterm PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
