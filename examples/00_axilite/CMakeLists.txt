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

cmake_minimum_required(VERSION 3.20)
project(00_axilite LANGUAGES CXX)


get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
include("${REPO_ROOT}/cmake/ProjectInit.cmake")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
include("${REPO_ROOT}/cmake/ProjectInit.cmake")

set(SLASH_PROJECT "example")
set(CFG_FILE "${CMAKE_CURRENT_LIST_DIR}/config.cfg")

set(HLS_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/hls")
set(DEVICE "xcv80-lsva4737-2MHP-e-S")
set(IP_REPO  "${HLS_BUILD_DIR}")
set(KERNELS
  "${HLS_BUILD_DIR}/build_accumulate.${DEVICE}/hls/impl/ip/component.xml"
  "${HLS_BUILD_DIR}/build_increment.${DEVICE}/hls/impl/ip/component.xml"
)

build_hw(
  TARGET     hw
  LINKER_DIR "${SLASH_LINKER_DIR}"
  PROJECT    "${SLASH_PROJECT}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
  IP_REPO    "${IP_REPO}"
)

build_sim(
  TARGET     sim
  LINKER_DIR "${SLASH_LINKER_DIR}"
  PROJECT    "${SLASH_PROJECT}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
)

build_emu(
  TARGET     emu
  LINKER_DIR "${SLASH_LINKER_DIR}"
  PROJECT    "${SLASH_PROJECT}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
)

# Include directories
include_directories(${REPO_ROOT}/vrt/include
                    /usr/local/vrt/include
                    /usr/local/include
                    /usr/include/ami
                    /usr/include/libxml2
                    /usr/include/jsoncpp)

set(HLS_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/hls")
set(DEVICE "xcv80-lsva4737-2MHP-e-S")
set(IP_REPO  "${HLS_BUILD_DIR}")
set(KERNELS
  "${HLS_BUILD_DIR}/build_accumulate.${DEVICE}/hls/impl/ip/component.xml"
  "${HLS_BUILD_DIR}/build_increment.${DEVICE}/hls/impl/ip/component.xml"
)

build_hw(
  TARGET     hw
  LINKER_DIR "${SLASH_LINKER_DIR}"
  PROJECT    "${SLASH_PROJECT}"
  CFG        "${CFG_FILE}"
  KERNELS    ${KERNELS}
  IP_REPO    "${IP_REPO}"
)

# Link the library to the executable
target_link_libraries(${PROJECT_NAME} vrt vrtd vrtdpp ami xml2 zmq jsoncpp)
