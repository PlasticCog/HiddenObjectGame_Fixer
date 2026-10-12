# SDL3 (static, built from the submodule) + Dear ImGui with the SDL3 + SDL_Renderer backends.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/SDL ${CMAKE_BINARY_DIR}/third_party/SDL EXCLUDE_FROM_ALL)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

add_executable(hoglauncher WIN32 main.cpp library.cpp app.rc)
set_target_properties(hoglauncher PROPERTIES OUTPUT_NAME "HOGFixer")
target_link_libraries(hoglauncher PRIVATE hogcommon imgui SDL3::SDL3-static comdlg32 shell32 ole32 uuid)
# WIN32 subsystem but we use main(); let MSVC find it.
if(MSVC)
    set_target_properties(hoglauncher PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()
