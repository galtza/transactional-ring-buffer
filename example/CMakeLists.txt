cmake_minimum_required(VERSION 3.8)
project (crc32)
 
# include library
include_directories(../include)

# output
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/.proj)

# verbose
set(CMAKE_VERBOSE_MAKEFILE off)

# include library
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "Build type ${CMAKE_BUILD_TYPE}")

# compiler specific flags
if (MSVC)
  add_compile_options(/W4 /WX)      # warning level 4 and all warnings as errors
  add_compile_options(/EHsc)        # no exceptions
  add_compile_options(/arch:SSE4.2) # SSE4.2
else()
  add_compile_options(-Wall -Wextra -pedantic -Werror) # warnings / errors
  add_compile_options(-fno-exceptions)                 # no exceptions
  add_compile_options(-msse4.2)                        # SSE4.2
endif()

# c++ standard
set(CMAKE_CXX_STANDARD 17)
 
# sources
set(SOURCES crc32.cpp)

# Executable
add_executable(crc32 ${SOURCES})

