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

function(build_emu)
  find_package(Vitis REQUIRED)
  set(options USE_SYMLINK)
  set(oneValueArgs TARGET LINKER_DIR EXAMPLE CFG OUT_DIR TB_TEMPLATE TB_OUT SYSTEM_MAP_OUT)
  set(multiValueArgs KERNELS)
  cmake_parse_arguments(BEMU "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT BEMU_TARGET)
    message(FATAL_ERROR "build_emu(): TARGET is required")
  endif()

  foreach(req LINKER_DIR EXAMPLE CFG)
    if("${BEMU_${req}}" STREQUAL "")
      message(FATAL_ERROR "build_emu(): ${req} is required")
    endif()
  endforeach()

  if(NOT BEMU_KERNELS)
    message(FATAL_ERROR "build_emu(): KERNELS is required (at least one component.xml)")
  endif()

  if("${BEMU_OUT_DIR}" STREQUAL "")
    set(BEMU_OUT_DIR "${CMAKE_BINARY_DIR}")
  endif()

  set(_main_py "")

  if(EXISTS "${BEMU_LINKER_DIR}/src/main.py")
    set(_main_py "${BEMU_LINKER_DIR}/src/main.py")
  elseif(EXISTS "${BEMU_LINKER_DIR}/main.py")
    set(_main_py "${BEMU_LINKER_DIR}/main.py")
  else()
    file(GLOB_RECURSE _main_candidates
      RELATIVE "${BEMU_LINKER_DIR}"
      "${BEMU_LINKER_DIR}/*/main.py"
      "${BEMU_LINKER_DIR}/main.py"
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
      set(_main_py "${BEMU_LINKER_DIR}/${_rel}")
    elseif(_n EQUAL 0)
      message(FATAL_ERROR
        "build_emu(): could not infer entrypoint. Expected one of:\n"
        "  ${BEMU_LINKER_DIR}/src/main.py\n"
        "  ${BEMU_LINKER_DIR}/main.py\n"
        "and could not find any other */main.py under LINKER_DIR."
      )
    else()
      string(REPLACE ";" "\n  " _cand_pretty "${_filtered}")
      message(FATAL_ERROR
        "build_emu(): multiple main.py candidates found under LINKER_DIR; ambiguous:\n"
        "  ${_cand_pretty}\n"
        "Please keep a single entrypoint, e.g. linker/src/main.py."
      )
    endif()
  endif()

  if(NOT EXISTS "${_main_py}")
    message(FATAL_ERROR "build_emu(): inferred entrypoint not found: '${_main_py}'")
  endif()

  get_filename_component(_main_dir "${_main_py}" DIRECTORY)

  if(NOT EXISTS "${BEMU_CFG}")
    message(FATAL_ERROR "build_emu(): CFG file not found: '${BEMU_CFG}'")
  endif()

  foreach(k IN LISTS BEMU_KERNELS)
    if(NOT EXISTS "${k}")
      message(FATAL_ERROR "build_emu(): kernel component.xml not found: '${k}'")
    endif()
  endforeach()

  if(DEFINED Python3_EXECUTABLE AND NOT "${Python3_EXECUTABLE}" STREQUAL "")
    set(_py "${Python3_EXECUTABLE}")
  else()
    set(_py "python3")
  endif()

  set(_emu_root "${BEMU_LINKER_DIR}/results/${BEMU_EXAMPLE}/sw_emu")
  set(_src_vpp "${_emu_root}/vpp_emu")
  set(_src_system_map "${BEMU_LINKER_DIR}/results/${BEMU_EXAMPLE}/system_map.xml")
  set(_src_vbin "${BEMU_LINKER_DIR}/results/${BEMU_EXAMPLE}/${BEMU_EXAMPLE}_emu.vrtbin")

  set(_dst_vbin "${BEMU_OUT_DIR}/${BEMU_EXAMPLE}_emu.vrtbin")
  set(_stamp "${BEMU_OUT_DIR}/.${BEMU_EXAMPLE}_emu.stamp")

  if(BEMU_USE_SYMLINK)
    set(_publish_vbin_cmd "${CMAKE_COMMAND}" -E create_symlink "${_src_vbin}" "${_dst_vbin}")
  else()
    set(_publish_vbin_cmd "${CMAKE_COMMAND}" -E copy_if_different "${_src_vbin}" "${_dst_vbin}")
  endif()

  set(_emu_args "--platform" "emu")
  if(NOT "${BEMU_TB_TEMPLATE}" STREQUAL "")
    list(APPEND _emu_args "--tb-template" "${BEMU_TB_TEMPLATE}")
  endif()
  if(NOT "${BEMU_TB_OUT}" STREQUAL "")
    list(APPEND _emu_args "--tb-out" "${BEMU_TB_OUT}")
  endif()
  if(NOT "${BEMU_SYSTEM_MAP_OUT}" STREQUAL "")
    list(APPEND _emu_args "--system-map-out" "${BEMU_SYSTEM_MAP_OUT}")
  endif()

  set(_extra_deps "")
  foreach(dep IN ITEMS BEMU_TB_TEMPLATE)
    if(NOT "${${dep}}" STREQUAL "")
      list(APPEND _extra_deps "${${dep}}")
    endif()
  endforeach()

  add_custom_command(
    OUTPUT "${_dst_vbin}" "${_stamp}"

    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BEMU_OUT_DIR}"

    # Run linker init (emulation platform)
    COMMAND "${_py}" "${_main_py}"
            init
            -p "${BEMU_EXAMPLE}"
            --cfg "${BEMU_CFG}"
            --kernels ${BEMU_KERNELS}
            ${_emu_args}

    # Build emulation project (generates tb.cpp + vpp_emu)
    COMMAND "${_py}" "${_main_py}"
            build_hw_project
            -p "${BEMU_EXAMPLE}"
            --emu

    # Package emulation artifacts into <project>_emu.vrtbin
    COMMAND "${_py}" "${_main_py}"
            create_metadata
            -p "${BEMU_EXAMPLE}"
            --emu

    COMMAND "${CMAKE_COMMAND}" -E echo "Publishing EMU vbin:"
    COMMAND "${CMAKE_COMMAND}" -E echo "  from ${_src_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E echo "  to   ${_dst_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${_dst_vbin}"
    COMMAND ${_publish_vbin_cmd}

    COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"

    WORKING_DIRECTORY "${_main_dir}"

    DEPENDS
      "${_main_py}"
      "${BEMU_CFG}"
      ${BEMU_KERNELS}
      ${_extra_deps}

    COMMENT "SLASH EMU build: ${BEMU_EXAMPLE} -> ${BEMU_OUT_DIR}"
    VERBATIM
  )

  add_custom_target("${BEMU_TARGET}" DEPENDS "${_dst_vbin}")

  set_property(TARGET "${BEMU_TARGET}" PROPERTY SLASH_EMU_ROOT "${_emu_root}")
  set_property(TARGET "${BEMU_TARGET}" PROPERTY SLASH_EMU_VPP "${_src_vpp}")
  set_property(TARGET "${BEMU_TARGET}" PROPERTY SLASH_SYSTEM_MAP "${_src_system_map}")
  set_property(TARGET "${BEMU_TARGET}" PROPERTY SLASH_EMU_VBIN "${_dst_vbin}")
endfunction()
