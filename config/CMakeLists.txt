# epicpinball-config: the settings tool (docs/config.md). Dear ImGui (MIT),
# pinned by its release and checked by SHA-256: on SDL2 and SDL's renderer
# (Linux), or on Win32 and Direct3D 11 with the game's DirectInput joysticks
# (Windows). The main CMakeLists.txt adds this with EPIC_CONFIG.
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
    URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

add_executable(epicpinball-config
    main.cpp sim.c vpx_import.c
    ../src/ini.c ../src/keys.c ../src/nudge.c ../src/controller_ini.c
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(epicpinball-config PRIVATE . ../src "${PCRECOMP_HOME}/runtime"
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(WIN32)
    target_sources(epicpinball-config PRIVATE ../src/dinput_joy.c
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    target_link_libraries(epicpinball-config PRIVATE d3d11 dxgi d3dcompiler dinput8 dxguid winmm)
    # a window, no console; main() as on Linux
    set_target_properties(epicpinball-config PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(epicpinball-config PRIVATE /ENTRY:mainCRTStartup)
    endif()
else()
    target_sources(epicpinball-config PRIVATE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
    target_link_libraries(epicpinball-config PRIVATE PkgConfig::SDL2 m)
endif()
# beside epicpinball, where epicpinball-config.sh and the game's Settings... look
set_target_properties(epicpinball-config PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
