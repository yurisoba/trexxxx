cmake_minimum_required(VERSION 3.18)
project(trexxxx)

include_directories(.)
add_executable(trexxxx main.cpp)

add_library(deps INTERFACE)
target_link_libraries(trexxxx deps)

find_package(glfw3 3.3 REQUIRED)
target_link_libraries(deps INTERFACE glfw)

find_package(OpenGL REQUIRED)
target_link_libraries(deps INTERFACE OpenGL::GL)

set(IMGUI_DIR imgui)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_sources(deps
        INTERFACE
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )