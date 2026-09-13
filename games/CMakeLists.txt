add_library(games_lib STATIC
    registry.cpp
    skyraider/game.cpp
    fighter/game.cpp
    keychase/game.cpp)

target_include_directories(games_lib PUBLIC
    ${CMAKE_SOURCE_DIR}            # 供 #include "games/<name>/game.h"
    ${CMAKE_CURRENT_SOURCE_DIR}    # 供 #include "registry.h"
    ${CMAKE_SOURCE_DIR}/engine/include)

target_link_libraries(games_lib PUBLIC fun_engine)
