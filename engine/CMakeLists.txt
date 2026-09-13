# ---------------------------------------------------------------------------
# 引擎本体(平台无关) —— 同时是一个**标准 Arduino 库**
#
# 目录布局刻意做成 Arduino 规范的样子: 头文件和 .cpp **都在 src/engine/ 下**。
#   · 引擎内部用的是同目录带引号的 include(#include "AssetStore.h"), 头和实现放在
#     一起之后一行都不用改;
#   · 外部一律写 #include "engine/xxx.h", 只要把 src/ 加进搜索路径就够。
# 于是这个目录本身就能被 Arduino IDE / PlatformIO 当普通库吃进去 —— 见同目录的
# library.properties(Arduino IDE) 与 library.json(PlatformIO)。
# ⚠ 别把文件搬回 include/ 了, 那会同时破坏 CMake 和 Arduino 两种消费方式。
# ---------------------------------------------------------------------------
add_library(fun_engine STATIC
    src/engine/Engine.cpp
    src/engine/Display.cpp
    src/engine/AudioMixer.cpp
    src/engine/Audio.cpp
    src/engine/AssetStore.cpp
    src/engine/RomFileSystem.cpp      # 内嵌素材(卡带 ROM): 由 tools/embed_assets.py 生成表
    src/engine/ScriptGame.cpp)        # .gbn 游戏包 + 字节码 VM

target_include_directories(fun_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)   # 供 #include "engine/xxx.h"
target_link_libraries(fun_engine PUBLIC lvgl)

# ---------------------------------------------------------------------------
# 重新生成内嵌素材表(可选, 没找到 Python 就手动跑 py -3 tools/embed_assets.py)
#   cmake --build <build-dir> --target embed-assets
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(embed-assets
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_assets.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "重新生成 engine/src/engine/rom_assets_data.h"
        VERBATIM)
else()
    message(STATUS "Python3 未找到, 需要重新内嵌素材时手动跑 py -3 tools/embed_assets.py")
endif()
