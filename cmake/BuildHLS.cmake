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

include_guard(GLOBAL)
find_package(Vitis REQUIRED)

function(build_hls)
  set(oneValueArgs TARGET CPP CFG DEVICE OUT_DIR)
  cmake_parse_arguments(BHL "" "${oneValueArgs}" "" ${ARGN})

  if(NOT BHL_TARGET)
    message(FATAL_ERROR "build_hls(): TARGET is required")
  endif()
  if(NOT BHL_CPP OR NOT BHL_CFG)
    message(FATAL_ERROR "build_hls(): CPP and CFG are required")
  endif()
  if(NOT BHL_DEVICE)
    message(FATAL_ERROR "build_hls(): DEVICE is required (e.g., xcv80-lsva4737-2MHP-e-S)")
  endif()

  get_filename_component(_cpp "${BHL_CPP}" REALPATH)
  get_filename_component(_cfg "${BHL_CFG}" REALPATH)

  if(NOT EXISTS "${_cpp}")
    message(FATAL_ERROR "build_hls(): CPP not found: '${_cpp}'")
  endif()
  if(NOT EXISTS "${_cfg}")
    message(FATAL_ERROR "build_hls(): CFG not found: '${_cfg}'")
  endif()

  if("${BHL_OUT_DIR}" STREQUAL "")
    set(BHL_OUT_DIR "${CMAKE_CURRENT_LIST_DIR}")
  endif()

  get_filename_component(_stem "${_cpp}" NAME_WE)
  set(_build_dir "${BHL_OUT_DIR}/build_${_stem}.${BHL_DEVICE}")
  set(_stamp "${_build_dir}/.hls_stamp")
  file(MAKE_DIRECTORY "${_build_dir}")

  find_program(VPP_EXECUTABLE NAMES v++)
  if(NOT VPP_EXECUTABLE)
    message(FATAL_ERROR "build_hls(): v++ not found. Ensure Vitis is installed and v++ is on PATH.")
  endif()

  find_program(VITIS_RUN_EXECUTABLE NAMES vitis-run)
  if(NOT VITIS_RUN_EXECUTABLE)
    message(FATAL_ERROR "build_hls(): vitis-run not found. Ensure Vitis is installed and vitis-run is on PATH.")
  endif()

  add_custom_command(
    OUTPUT "${_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_build_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_cpp}" "${_build_dir}/${_stem}.cpp"
    COMMAND "${VPP_EXECUTABLE}" -c --mode hls --config "${_cfg}" --work_dir .
    COMMAND "${VITIS_RUN_EXECUTABLE}" --mode hls --package --config "${_cfg}" --work_dir .
    COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
    WORKING_DIRECTORY "${_build_dir}"
    DEPENDS "${_cpp}" "${_cfg}"
    COMMENT "HLS build: ${_stem}"
    VERBATIM
  )

  add_custom_target("${BHL_TARGET}" DEPENDS "${_stamp}")
  set_property(TARGET "${BHL_TARGET}" PROPERTY HLS_BUILD_DIR "${_build_dir}")
  set("${BHL_TARGET}_BUILD_DIR" "${_build_dir}" PARENT_SCOPE)
endfunction()

function(build_hls_clean)
  set(oneValueArgs TARGET DEVICE ROOT)
  set(multiValueArgs EXTRA_GLOBS)
  cmake_parse_arguments(BHLC "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT BHLC_TARGET)
    message(FATAL_ERROR "build_hls_clean(): TARGET is required")
  endif()
  if(NOT BHLC_DEVICE)
    message(FATAL_ERROR "build_hls_clean(): DEVICE is required")
  endif()

  if("${BHLC_ROOT}" STREQUAL "")
    set(BHLC_ROOT "${CMAKE_CURRENT_LIST_DIR}")
  endif()

  set(_patterns
    "${BHLC_ROOT}/build_*.${BHLC_DEVICE}"
    "${BHLC_ROOT}/*.log"
    "${BHLC_ROOT}/.Xil"
    "${BHLC_ROOT}/CMakeCache.txt"
    "${BHLC_ROOT}/CMakeFiles"
    "${BHLC_ROOT}/cmake_install.cmake"
    "${BHLC_ROOT}/Makefile"
    "${BHLC_ROOT}/${BHLC_TARGET}.cmake"
  )
  if(BHLC_EXTRA_GLOBS)
    list(APPEND _patterns ${BHLC_EXTRA_GLOBS})
  endif()

  set(_clean_script "${CMAKE_CURRENT_BINARY_DIR}/${BHLC_TARGET}.cmake")
  set(_script "message(STATUS \"Cleaning HLS build outputs\")\n")
  foreach(p IN LISTS _patterns)
    string(APPEND _script "file(GLOB _matches \"${p}\")\n")
    string(APPEND _script "if(_matches)\n  file(REMOVE_RECURSE \${_matches})\nendif()\n")
  endforeach()
  file(WRITE "${_clean_script}" "${_script}")

  add_custom_target("${BHLC_TARGET}"
    COMMAND "${CMAKE_COMMAND}" -P "${_clean_script}"
    COMMENT "Cleaning HLS build outputs"
  )
endfunction()
