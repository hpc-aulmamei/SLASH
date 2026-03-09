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


# TODOs:
# * Allow arguments of add_vbin to be relative to source directory
# * Find the installed v80++ executable
# * Add wrappers for building HLS cores

find_package(Vivado REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(SLASH_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(SLASH_LINKER_DIR "${SLASH_REPO_ROOT}/linker/")
set(SLASH_MAIN_PY "${SLASH_LINKER_DIR}/src/main.py")

if(EXISTS ${SLASH_MAIN_PY})
    message(STATUS "Found Slash at ${SLASH_REPO_ROOT}.")
    set(SLASH_FOUND TRUE)
else()
    message(STATUS "Slash not found.")
    set(SLASH_FOUND FALSE)
endif()

function(add_vbin)
    set(oneValueArgs TARGET CFG PLATFORM IP_REPO)
    set(multiValueArgs KERNELS)
    cmake_parse_arguments(SLASH_VBIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach(req TARGET CFG PLATFORM KERNELS)
        if("${SLASH_VBIN_${req}}" STREQUAL "")
        message(FATAL_ERROR "add_vbin: ${req} is required")
        endif()
    endforeach()

    set(SLASH_VBIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SLASH_VBIN_TARGET}.vbin")
    
    if(DEFINED Python3_EXECUTABLE AND NOT "${Python3_EXECUTABLE}" STREQUAL "")
        set(_py "${Python3_EXECUTABLE}")
    else()
        set(_py "python3")
    endif()

    add_custom_command(
        OUTPUT "${SLASH_VBIN_FILE}"
        COMMAND "${_py}" "${SLASH_MAIN_PY}" "link" 
            "-c" "${SLASH_VBIN_CFG}"
            "-p" "${SLASH_VBIN_PLATFORM}"
            "-o" "${SLASH_VBIN_FILE}"
            "-k" ${SLASH_VBIN_KERNELS}
            "--ip-repository" "${SLASH_VBIN_IP_REPO}"
            "--vivado" "${VIVADO_BINARY}"
        BYPRODUCTS "${SLASH_VBIN_FILE}.prj"
        DEPENDS "${SLASH_VBIN_CFG}" "${SLASH_VBIN_KERNELS}"
        WORKING_DIRECTORY "${SLASH_LINKER_DIR}/src"
    )
    add_custom_target("${SLASH_VBIN_TARGET}" DEPENDS "${SLASH_VBIN_FILE}")
endfunction()
