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
project(04_freq LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-O3")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${REPO_ROOT}/cmake")
find_package(Slash REQUIRED)

set(CFG_FILE "${CMAKE_CURRENT_LIST_DIR}/config.cfg")

set(DEVICE "xcv80-lsva4737-2MHP-e-S")
set(IP_REPO  "${CMAKE_CURRENT_LIST_DIR}/hls")
set(KERNELS
  "${IP_REPO}/build_vadd.${DEVICE}/hls/impl/ip/component.xml"
)

add_vbin(
  TARGET    "freq_hw"
  PLATFORM  "hw"
  CFG       "${CFG_FILE}"
  KERNELS   ${KERNELS}
)

add_vbin(
  TARGET      "freq_sim"
  PLATFORM    "sim"
  CFG         "${CFG_FILE}"
  KERNELS     ${KERNELS}
)

add_vbin(
  TARGET      "freq_emu"
  PLATFORM    "emu"
  CFG         "${CFG_FILE}"
  KERNELS     ${KERNELS}
)

# Include directories
# Keep headers in lockstep with the installed shared libraries (linked below).
include_directories(${REPO_ROOT}/vrt/include
                    /usr/local/vrt/include
                    /usr/local/include
                    /usr/include/libxml2
                    /usr/include/jsoncpp)

# Define sources for the executable
set(EXE_SOURCES 04_freq.cpp)

# Create the executable
add_executable(${PROJECT_NAME} ${EXE_SOURCES})

# Link the library to the executable
target_link_libraries(${PROJECT_NAME} vrt vrtd vrtdpp xml2 zmq jsoncpp)
