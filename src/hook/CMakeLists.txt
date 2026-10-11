add_library(hogfix SHARED
    dllmain.cpp
    log.cpp
    hooks.cpp
    sigscan.cpp
    display.cpp
    window.cpp
    cursor.cpp
    framepace.cpp
    d3d9_layer.cpp
    engine_shim.cpp
    hogfix.def)
set_target_properties(hogfix PROPERTIES OUTPUT_NAME "hogfix")
target_include_directories(hogfix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hogfix PRIVATE hogcommon minhook)
target_compile_definitions(hogfix PRIVATE DIRECTINPUT_VERSION=0x0800)
