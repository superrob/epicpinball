# epicpinball-dof: GPLv3, apart from the game (dof/LICENSE). Use build.sh
# (Linux) or build.ps1 (Windows), which fetch and build the pinned libdof this links.
cmake_minimum_required(VERSION 3.25)
project(epicpinball_dof CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/W3 /EHsc /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(autorow_test tests/autorow_test.cpp)

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/build/lib")
set(LIBDOF "${CMAKE_CURRENT_SOURCE_DIR}/build/src/libdof")
add_executable(epicpinball-dof src/main.cpp)
target_include_directories(epicpinball-dof SYSTEM PRIVATE "${LIBDOF}/include")
target_link_directories(epicpinball-dof PRIVATE "${LIBS}")
find_package(Threads REQUIRED)
if(WIN32)
    # build.ps1 puts libdof's import library (dof64.lib) in build/lib, and the
    # DLLs beside the helper in dof/build
    target_link_libraries(epicpinball-dof PRIVATE dof64 Threads::Threads)
else()
    target_link_libraries(epicpinball-dof PRIVATE dof serialport hidapi-hidraw ftdi1 usb-1.0 Threads::Threads)
    # the copy build.sh puts in dof/build finds its libraries beside it
    set_target_properties(epicpinball-dof PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "\$ORIGIN/lib"
        LINK_FLAGS "-Wl,--disable-new-dtags")
endif()
