# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
# 
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
# 
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################

cmake_minimum_required(VERSION 3.10)
project(
  vrt

  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VRT_INCLUDE_VRTD "Include vrtd subdirectory instead of building from system" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_compile_options("-Wunused-result")

file(
  GLOB
  LIB_SOURCES
  
  ${CMAKE_SOURCE_DIR}/src/allocator/*.cpp
  ${CMAKE_SOURCE_DIR}/include/buffer/*.hpp
  ${CMAKE_SOURCE_DIR}/src/qdma/*.cpp
  ${CMAKE_SOURCE_DIR}/src/*.cpp 
  ${CMAKE_SOURCE_DIR}/src/parser/*.cpp
  ${CMAKE_SOURCE_DIR}/src/register/*.cpp
  ${CMAKE_SOURCE_DIR}/src/driver/*.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/*.cpp
)

add_library(vrt SHARED ${LIB_SOURCES})

add_library(vrt::vrt ALIAS vrt)

target_include_directories(
  vrt
  
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
)

set_target_properties(vrt PROPERTIES

  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)


if(VRT_INCLUDE_VRTD)
  add_subdirectory(vrtd vrtd)
else()
  find_package(vrtd REQUIRED CONFIG)
endif()

if(NOT TARGET vrtd::libvrtdpp)
    message(FATAL_ERROR
        "vrtd package is missing target vrtd::libvrtdpp. "
        "Build and install vrtd first (cmake --install), then configure vrt again.")
endif()

find_package(ZLIB REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

if(NOT TARGET jsoncpp_lib AND TARGET JsonCpp::JsonCpp)
  add_library(jsoncpp_lib ALIAS JsonCpp::JsonCpp)
endif()

# find_package(ZeroMQ CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_ZMQ REQUIRED IMPORTED_TARGET libzmq)

target_link_libraries(vrt PUBLIC PkgConfig::PC_ZMQ)

target_link_libraries(vrt
  PUBLIC
    vrtd::libvrtdpp
    ZLIB::ZLIB
    LibXml2::LibXml2
    jsoncpp_lib
    PkgConfig::PC_ZMQ
)

set_target_properties(vrt PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/include/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  TARGETS vrt
  EXPORT  vrtTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# -------- CMake package configuration --------
# Generate the version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/vrtConfigVersion.cmake"
  VERSION       ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion
)

# Configure the main package config from template
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/vrtConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/vrtConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrt"
)

# Export targets for *install* tree
install(
  EXPORT vrtTargets
  NAMESPACE vrt::
  FILE vrtTargets.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrt"
)

# Install the config + version files
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrtConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrtConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrt"
)

# Export targets for *build* tree so a project can use this directory directly
export(
  EXPORT vrtTargets
  NAMESPACE vrt::
  FILE "${CMAKE_CURRENT_BINARY_DIR}/vrtTargets.cmake"
)
