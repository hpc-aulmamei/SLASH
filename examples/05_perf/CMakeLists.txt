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
project(05_perf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SLASH_USE_REPO "Build against the local repo tree instead of installed packages" OFF)

if(SLASH_USE_REPO)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
  list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
  include(SlashTools)
  set(_VRT_INCLUDES ${REPO_ROOT}/vrt/include /usr/include/ami)
  set(_VRT_LIBS vrt vrtd vrtdpp ami xml2 zmq jsoncpp)
else()
  find_package(vrt REQUIRED CONFIG)
  find_package(SlashTools REQUIRED)
  set(_VRT_INCLUDES "")
  set(_VRT_LIBS vrt::vrt ami)
endif()

set(CFG_FILE "${CMAKE_CURRENT_LIST_DIR}/config.cfg")

set(DEVICE "xcv80-lsva4737-2MHP-e-S")
set(IP_REPO "${CMAKE_CURRENT_LIST_DIR}/hls")
set(KERNELS
  "${IP_REPO}/build_perf.${DEVICE}/hls/impl/ip/component.xml"
)

add_vbin(
  TARGET    "perf_hw"
  PLATFORM  "hw"
  CFG       "${CFG_FILE}"
  KERNELS   ${KERNELS}
)

add_vbin(
  TARGET      "perf_sim"
  PLATFORM    "sim"
  CFG         "${CFG_FILE}"
  KERNELS     ${KERNELS}
)

add_vbin(
  TARGET      "perf_emu"
  PLATFORM    "emu"
  CFG         "${CFG_FILE}"
  KERNELS     ${KERNELS}
)

add_executable(${PROJECT_NAME} 05_perf.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${_VRT_INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${_VRT_LIBS})
