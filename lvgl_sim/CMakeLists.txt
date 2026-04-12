cmake_minimum_required(VERSION 3.16)
project(lvgl_sim CXX C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

set(LVGL_DIR    ${CMAKE_CURRENT_LIST_DIR}/../lvgl)
set(CAT_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../cyber_cat/src)

set(LV_BUILD_CONF_PATH   ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h)
set(CONFIG_LV_USE_SDL    1 CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    0 CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES 0 CACHE BOOL "" FORCE)

add_subdirectory(${LVGL_DIR} lvgl_build)

add_executable(lvgl_sim
    main.cpp
    ${CAT_SRC_DIR}/cat_face.cpp
)

find_package(SDL2 REQUIRED)

target_include_directories(lvgl_sim PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/mock   # Arduino.h + Touch_CST816.h stubs (highest priority)
    ${CAT_SRC_DIR}                   # cat_face.h
    ${LVGL_DIR}                      # lvgl.h
    ${SDL2_INCLUDE_DIRS}             # SDL2/SDL.h
)

target_link_libraries(lvgl_sim PRIVATE lvgl ${SDL2_LIBRARIES})
