cmake_minimum_required(VERSION 2.8.9)
project (unit_tests)
 
# include library
include_directories(../include)

# verbose
set( CMAKE_VERBOSE_MAKEFILE off)

# include library
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" FORCE)
endif(NOT CMAKE_BUILD_TYPE)

# compiler specific flags
if (MSVC)
  add_compile_options(/W4 /WX) # warning level 4 and all warnings as errors
  add_compile_options(/EHsc)   # no exceptions
else()
  add_compile_options(-Wall -Wextra -pedantic -Werror) # warnings / errors
  add_compile_options(-fno-exceptions)                 # no exceptions
endif()

# c++ standard
set(CMAKE_CXX_STANDARD 11)
 
# sources
set(SOURCES unit_tests.cpp)

# Executable
add_executable(unit_tests ${SOURCES})

