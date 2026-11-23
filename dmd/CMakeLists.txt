# epicpinball-dmd: GPLv3, apart from the game (dmd/LICENSE). Use build.sh,
# which fetches and builds the pinned libdmdutil this links.
cmake_minimum_required(VERSION 3.25)
project(epicpinball_dmd CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/build/lib")
set(LIBDMDUTIL "${CMAKE_CURRENT_SOURCE_DIR}/build/src/libdmdutil")
add_executable(epicpinball-dmd src/main.cpp)
target_include_directories(epicpinball-dmd SYSTEM PRIVATE "${LIBDMDUTIL}/include")
target_link_directories(epicpinball-dmd PRIVATE "${LIBS}")
find_package(Threads REQUIRED)
target_link_libraries(epicpinball-dmd PRIVATE dmdutil Threads::Threads)
# the copy build.sh puts in dmd/build finds its libraries beside it
set_target_properties(epicpinball-dmd PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "\$ORIGIN/lib"
    LINK_FLAGS "-Wl,--disable-new-dtags")
