# libftdi-msvc.cmake - read by libftdi's CMake after its project() (dof/build.ps1
# passes it as CMAKE_PROJECT_INCLUDE): every C file of libftdi gets
# msvc-gettimeofday.h first. Not in CMAKE_C_FLAGS, where CMake's own compiler
# check would be given it too, and fail.
add_compile_options("$<$<COMPILE_LANGUAGE:C>:/FI${CMAKE_CURRENT_LIST_DIR}/msvc-gettimeofday.h>")
