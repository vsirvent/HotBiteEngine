cmake_minimum_required(VERSION 3.21.21080301.0)
cmake_policy(SET CMP0091 OLD)
cmake_policy(SET CMP0126 OLD)
project(CMAKE_TRY_COMPILE C)
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")
set(CMAKE_VERBOSE_MAKEFILE 1)
set(CMAKE_C_FLAGS "/DWIN32 /D_WINDOWS /W3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILE_DEFINITIONS}")
set(CMAKE_C_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /RTC1")
set(CMAKE_EXE_LINKER_FLAGS "/machine:x64")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EXE_LINKER_FLAGS}")
include_directories(${INCLUDE_DIRECTORIES})
set(CMAKE_SUPPRESS_REGENERATION 1)
link_directories(${LINK_DIRECTORIES})
cmake_policy(SET CMP0065 NEW)
cmake_policy(SET CMP0083 OLD)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "C:/Users/Vicen/source/repos/vsirvent/React3d/test/out/build/x64-Debug/CMakeFiles/CMakeTmp")
add_executable(cmTC_5fc61 "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/share/cmake-3.21/Modules/CMakeCCompilerABI.c")
target_link_libraries(cmTC_5fc61 ${LINK_LIBRARIES})
