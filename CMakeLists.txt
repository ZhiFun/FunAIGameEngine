cmake_minimum_required(VERSION 3.16)
# 版本号唯一来源是 engine/src/engine/Version.h —— 改版本时两边一起改
project(FunAIGameEngine VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# LVGL 8.3.11 —— 本地自带(third_party/lvgl), 无需联网 clone。
#   custom.cmake 里 LV_CONF_INCLUDE_SIMPLE 默认 ON, 再把 platform/sim 加进
#   include path, lvgl 就会直接 #include "lv_conf.h" 拿到我们的配置。
# ---------------------------------------------------------------------------
add_subdirectory(third_party/lvgl)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/sim)

# ---------------------------------------------------------------------------
# Qt(仅模拟器; 参考 FunKeyboardTools: Qt 6.9.3 MinGW)
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64
#   模拟器音频用 Qt Multimedia(QAudioSink), 不再依赖 miniaudio/SDL。
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets Multimedia)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 (Widgets+Multimedia) not found. Simulator target (fun_sim) will be skipped. "
                    "Pass -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 (or your Qt kit path).")
endif()

add_subdirectory(engine)
add_subdirectory(games)

if(Qt6_FOUND)
    add_subdirectory(platform/sim)
endif()
