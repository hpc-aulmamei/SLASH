# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
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
project(vrt-api)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("-Wunused-result")

include_directories(${CMAKE_SOURCE_DIR}/include/ ${CMAKE_SOURCE_DIR}/src/ /usr/include/ami/ /usr/include/libxml2 /usr/include/jsoncpp)

file(GLOB LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/allocator/*.cpp ${CMAKE_SOURCE_DIR}/include/buffer/*.hpp
${CMAKE_SOURCE_DIR}/src/qdma/*.cpp ${CMAKE_SOURCE_DIR}/src/api/*.cpp 
${CMAKE_SOURCE_DIR}/src/parser/*.cpp ${CMAKE_SOURCE_DIR}/src/register/*.cpp ${CMAKE_SOURCE_DIR}/src/driver/*.cpp
${CMAKE_SOURCE_DIR}/src/utils/*.cpp)

add_library(vrt SHARED ${LIB_SOURCES})

find_package(vrtd REQUIRED CONFIG)
find_package(ZLIB REQUIRED)
if(NOT TARGET vrtd::libvrtdpp)
    message(FATAL_ERROR
        "vrtd package is missing target vrtd::libvrtdpp. "
        "Build and install vrtd first (cmake --install), then configure vrt again.")
endif()

target_link_libraries(vrt PUBLIC vrtd::libvrtdpp ZLIB::ZLIB)

set_target_properties(vrt PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

install(TARGETS vrt
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION vrt/include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/ DESTINATION vrt
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
