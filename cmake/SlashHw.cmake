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
include(FindVivado)

function(build_hw)
  set(options USE_SYMLINK)
  set(oneValueArgs TARGET LINKER_DIR PROJECT CFG IP_REPO OUT_DIR)
  set(multiValueArgs KERNELS)
  cmake_parse_arguments(BHW "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT BHW_TARGET)
    message(FATAL_ERROR "build_hw(): TARGET is required")
  endif()

  foreach(req LINKER_DIR PROJECT CFG IP_REPO)
    if("${BHW_${req}}" STREQUAL "")
      message(FATAL_ERROR "build_hw(): ${req} is required")
    endif()
  endforeach()

  if(NOT BHW_KERNELS)
    message(FATAL_ERROR "build_hw(): KERNELS is required (at least one component.xml)")
  endif()

  if("${BHW_OUT_DIR}" STREQUAL "")
    set(BHW_OUT_DIR "${CMAKE_BINARY_DIR}")
  endif()

  set(_main_py "")

  if(EXISTS "${BHW_LINKER_DIR}/src/main.py")
    set(_main_py "${BHW_LINKER_DIR}/src/main.py")
  elseif(EXISTS "${BHW_LINKER_DIR}/main.py")
    set(_main_py "${BHW_LINKER_DIR}/main.py")
  else()
    file(GLOB_RECURSE _main_candidates
      RELATIVE "${BHW_LINKER_DIR}"
      "${BHW_LINKER_DIR}/*/main.py"
      "${BHW_LINKER_DIR}/main.py"
    )

    set(_filtered "")
    foreach(p IN LISTS _main_candidates)
      if(p MATCHES "^\\.venv/" OR p MATCHES "^venv/" OR p MATCHES "^build/" OR p MATCHES "^dist/" OR p MATCHES "^__pycache__/")
        # skip
      else()
        list(APPEND _filtered "${p}")
      endif()
    endforeach()

    list(LENGTH _filtered _n)
    if(_n EQUAL 1)
      list(GET _filtered 0 _rel)
      set(_main_py "${BHW_LINKER_DIR}/${_rel}")
    elseif(_n EQUAL 0)
      message(FATAL_ERROR
        "build_hw(): could not infer entrypoint. Expected one of:\n"
        "  ${BHW_LINKER_DIR}/src/main.py\n"
        "  ${BHW_LINKER_DIR}/main.py\n"
        "and could not find any other */main.py under LINKER_DIR."
      )
    else()
      string(REPLACE ";" "\n  " _cand_pretty "${_filtered}")
      message(FATAL_ERROR
        "build_hw(): multiple main.py candidates found under LINKER_DIR; ambiguous:\n"
        "  ${_cand_pretty}\n"
        "Please keep a single entrypoint, e.g. linker/src/main.py."
      )
    endif()
  endif()

  if(NOT EXISTS "${_main_py}")
    message(FATAL_ERROR "build_hw(): inferred entrypoint not found: '${_main_py}'")
  endif()

  get_filename_component(_main_dir "${_main_py}" DIRECTORY)

  if(NOT EXISTS "${BHW_CFG}")
    message(FATAL_ERROR "build_hw(): CFG file not found: '${BHW_CFG}'")
  endif()

  foreach(k IN LISTS BHW_KERNELS)
    if(NOT EXISTS "${k}")
      message(FATAL_ERROR "build_hw(): kernel component.xml not found: '${k}'")
    endif()
  endforeach()

  if(NOT IS_DIRECTORY "${BHW_IP_REPO}")
    message(FATAL_ERROR "build_hw(): IP_REPO is not a directory: '${BHW_IP_REPO}'")
  endif()

  if(DEFINED Python3_EXECUTABLE AND NOT "${Python3_EXECUTABLE}" STREQUAL "")
    set(_py "${Python3_EXECUTABLE}")
  else()
    set(_py "python3")
  endif()


  set(_src_vbin "${BHW_LINKER_DIR}/results/${BHW_PROJECT}/${BHW_PROJECT}_hw.vbin")
  set(_dst_vbin "${BHW_OUT_DIR}/${BHW_PROJECT}_hw.vbin")
  set(_stamp    "${BHW_OUT_DIR}/.${BHW_PROJECT}_linker.stamp")

  if(BHW_USE_SYMLINK)
    set(_publish_cmd "${CMAKE_COMMAND}" -E create_symlink "${_src_vbin}" "${_dst_vbin}")
  else()
    set(_publish_cmd "${CMAKE_COMMAND}" -E copy_if_different "${_src_vbin}" "${_dst_vbin}")
  endif()

  add_custom_command(
    OUTPUT "${_dst_vbin}" "${_stamp}"

    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BHW_OUT_DIR}"

    # Run linker
    COMMAND "${_py}" "${_main_py}"
            -p "${BHW_PROJECT}"
            --cfg "${BHW_CFG}"
            --kernels ${BHW_KERNELS}
            --ip-repository "${BHW_IP_REPO}"

    COMMAND "${CMAKE_COMMAND}" -E echo "Publishing HW vbin:"
    COMMAND "${CMAKE_COMMAND}" -E echo "  from ${_src_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E echo "  to   ${_dst_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E remove -f "${_dst_vbin}"
    COMMAND ${_publish_cmd}

    COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"

    WORKING_DIRECTORY "${_main_dir}"

    DEPENDS
      "${_main_py}"
      "${BHW_CFG}"
      ${BHW_KERNELS}

    COMMENT "SLASH HW build: ${BHW_PROJECT} -> ${_dst_vbin}"
    VERBATIM
  )

  add_custom_target("${BHW_TARGET}" DEPENDS "${_dst_vbin}")

  set_property(TARGET "${BHW_TARGET}" PROPERTY SLASH_VBIN "${_dst_vbin}")
  set_property(TARGET "${BHW_TARGET}" PROPERTY SLASH_VBIN_SRC "${_src_vbin}")
endfunction()
