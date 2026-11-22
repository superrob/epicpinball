# epicpinball-dof: GPLv3, apart from the game (dof/LICENSE). Use build.sh,
# which fetches and builds the pinned libdof this links.
cmake_minimum_required(VERSION 3.25)
project(epicpinball_dof CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

add_executable(autorow_test tests/autorow_test.cpp)

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/build/lib")
set(LIBDOF "${CMAKE_CURRENT_SOURCE_DIR}/build/src/libdof")
add_executable(epicpinball-dof src/main.cpp)
target_include_directories(epicpinball-dof SYSTEM PRIVATE "${LIBDOF}/include")
target_link_directories(epicpinball-dof PRIVATE "${LIBS}")
find_package(Threads REQUIRED)
target_link_libraries(epicpinball-dof PRIVATE dof serialport hidapi-hidraw ftdi1 usb-1.0 Threads::Threads)
# the copy build.sh puts in dof/build finds its libraries beside it
set_target_properties(epicpinball-dof PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "\$ORIGIN/lib"
    LINK_FLAGS "-Wl,--disable-new-dtags")
