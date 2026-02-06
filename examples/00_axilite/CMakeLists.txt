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

# cmake_minimum_required(VERSION 3.10)
# project(00_axilite)

# set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_FLAGS "-O3")

# # Include directories
# include_directories(/usr/local/vrt/include
#                     /usr/include/ami
#                     /usr/include/libxml2
#                     /usr/include/jsoncpp)

# # Define sources for the executable
# set(EXE_SOURCES 00_axilite.cpp)

# # Create the executable
# add_executable(${PROJECT_NAME} ${EXE_SOURCES})

# # Link the library to the executable
# target_link_libraries(${PROJECT_NAME} vrt ami xml2 zmq jsoncpp)

cmake_minimum_required(VERSION 3.20)
project(00_axilite LANGUAGES NONE)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
include("${REPO_ROOT}/cmake/ExampleInit.cmake")

set(EXAMPLE_NAME "example")
set(HLS_DEVICE "xcv80-lsva4737-2MHP-e-S")
set(CFG_FILE "${CMAKE_CURRENT_LIST_DIR}/config.cfg")

# Build HLS kernels up front (configure-time) so kernel paths exist before linker setup.
set(HLS_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/hls")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${HLS_BUILD_DIR}" -B "${HLS_BUILD_DIR}" -DDEVICE=${HLS_DEVICE}
  RESULT_VARIABLE _hls_cfg_res
)
if(NOT _hls_cfg_res EQUAL 0)
  message(FATAL_ERROR "Failed to configure HLS build (${_hls_cfg_res}).")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${HLS_BUILD_DIR}" --target hls
  RESULT_VARIABLE _hls_build_res
)
if(NOT _hls_build_res EQUAL 0)
  message(FATAL_ERROR "Failed to build HLS kernels (${_hls_build_res}).")
endif()

set(IP_REPO  "${HLS_BUILD_DIR}")
set(KERNELS
  "${HLS_BUILD_DIR}/build_accumulate.${HLS_DEVICE}/hls/impl/ip/component.xml"
  "${HLS_BUILD_DIR}/build_increment.${HLS_DEVICE}/hls/impl/ip/component.xml"
)

build_hw(
  TARGET     hw
  LINKER_DIR "${SLASH_LINKER_DIR}"
  EXAMPLE    "${EXAMPLE_NAME}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
  IP_REPO    "${IP_REPO}"
)

build_sim(
  TARGET     sim
  LINKER_DIR "${SLASH_LINKER_DIR}"
  EXAMPLE    "${EXAMPLE_NAME}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
)

build_emu(
  TARGET     emu
  LINKER_DIR "${SLASH_LINKER_DIR}"
  EXAMPLE    "${EXAMPLE_NAME}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
)
