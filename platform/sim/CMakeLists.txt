set(CMAKE_AUTOMOC ON)

qt_add_executable(fun_sim
    main.cpp
    launcher.cpp
    launcher.h
    sim_paths.cpp
    sim_paths.h
    sim_platform.cpp
    sim_platform.h
    sim_window.cpp
    sim_window.h
    sim_audio.cpp
    sim_audio.h)

target_include_directories(fun_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(fun_sim PRIVATE
    fun_engine
    games_lib
    lvgl
    Qt6::Widgets
    Qt6::Multimedia)
