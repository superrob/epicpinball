# epicpinball-dmd: GPLv3, apart from the game (dmd/LICENSE). Use build.sh
# (Linux) or build.ps1 (Windows), which fetch and build the pinned libdmdutil this links.
cmake_minimum_required(VERSION 3.25)
project(epicpinball_dmd CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/W3 /EHsc /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/build/lib")
set(LIBDMDUTIL "${CMAKE_CURRENT_SOURCE_DIR}/build/src/libdmdutil")
add_executable(epicpinball-dmd src/main.cpp)
target_include_directories(epicpinball-dmd SYSTEM PRIVATE "${LIBDMDUTIL}/include")
target_link_directories(epicpinball-dmd PRIVATE "${LIBS}")
find_package(Threads REQUIRED)
if(WIN32)
    # build.ps1 puts libdmdutil's import library (dmdutil64.lib) in build/lib,
    # and the DLLs beside the helper in dmd/build
    target_link_libraries(epicpinball-dmd PRIVATE dmdutil64 Threads::Threads)
else()
    target_link_libraries(epicpinball-dmd PRIVATE dmdutil Threads::Threads)
    # the copy build.sh puts in dmd/build finds its libraries beside it
    set_target_properties(epicpinball-dmd PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "\$ORIGIN/lib"
        LINK_FLAGS "-Wl,--disable-new-dtags")
endif()
