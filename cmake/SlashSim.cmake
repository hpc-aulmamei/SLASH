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

function(build_sim)
  set(options USE_SYMLINK)
  set(oneValueArgs TARGET LINKER_DIR PROJECT CFG OUT_DIR SIM_TEMPLATE SIM_MEM SIM_OUT SYSTEM_MAP_OUT)
  set(multiValueArgs KERNELS)
  cmake_parse_arguments(BSIM "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT BSIM_TARGET)
    message(FATAL_ERROR "build_sim(): TARGET is required")
  endif()

  foreach(req LINKER_DIR PROJECT CFG)
    if("${BSIM_${req}}" STREQUAL "")
      message(FATAL_ERROR "build_sim(): ${req} is required")
    endif()
  endforeach()

  if(NOT BSIM_KERNELS)
    message(FATAL_ERROR "build_sim(): KERNELS is required (at least one component.xml)")
  endif()

  if("${BSIM_OUT_DIR}" STREQUAL "")
    set(BSIM_OUT_DIR "${CMAKE_BINARY_DIR}")
  endif()

  set(_main_py "")

  if(EXISTS "${BSIM_LINKER_DIR}/src/main.py")
    set(_main_py "${BSIM_LINKER_DIR}/src/main.py")
  elseif(EXISTS "${BSIM_LINKER_DIR}/main.py")
    set(_main_py "${BSIM_LINKER_DIR}/main.py")
  else()
    file(GLOB_RECURSE _main_candidates
      RELATIVE "${BSIM_LINKER_DIR}"
      "${BSIM_LINKER_DIR}/*/main.py"
      "${BSIM_LINKER_DIR}/main.py"
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
      set(_main_py "${BSIM_LINKER_DIR}/${_rel}")
    elseif(_n EQUAL 0)
      message(FATAL_ERROR
        "build_sim(): could not infer entrypoint. Expected one of:\n"
        "  ${BSIM_LINKER_DIR}/src/main.py\n"
        "  ${BSIM_LINKER_DIR}/main.py\n"
        "and could not find any other */main.py under LINKER_DIR."
      )
    else()
      string(REPLACE ";" "\n  " _cand_pretty "${_filtered}")
      message(FATAL_ERROR
        "build_sim(): multiple main.py candidates found under LINKER_DIR; ambiguous:\n"
        "  ${_cand_pretty}\n"
        "Please keep a single entrypoint, e.g. linker/src/main.py."
      )
    endif()
  endif()

  if(NOT EXISTS "${_main_py}")
    message(FATAL_ERROR "build_sim(): inferred entrypoint not found: '${_main_py}'")
  endif()

  get_filename_component(_main_dir "${_main_py}" DIRECTORY)

  if(NOT EXISTS "${BSIM_CFG}")
    message(FATAL_ERROR "build_sim(): CFG file not found: '${BSIM_CFG}'")
  endif()

  foreach(k IN LISTS BSIM_KERNELS)
    if(NOT EXISTS "${k}")
      message(FATAL_ERROR "build_sim(): kernel component.xml not found: '${k}'")
    endif()
  endforeach()

  if(DEFINED Python3_EXECUTABLE AND NOT "${Python3_EXECUTABLE}" STREQUAL "")
    set(_py "${Python3_EXECUTABLE}")
  else()
    set(_py "python3")
  endif()

  set(_sim_root "${BSIM_LINKER_DIR}/results/${BSIM_PROJECT}/sim")
  set(_src_vpp "${_sim_root}/vpp_sim")
  set(_src_xsim "${_sim_root}/xsim.dir")
  set(_src_system_map "${_sim_root}/system_map.xml")
  set(_src_vbin "${_sim_root}/${BSIM_PROJECT}_sim.vbin")

  set(_dst_vpp "${BSIM_OUT_DIR}/vpp_sim")
  set(_dst_xsim "${BSIM_OUT_DIR}/xsim.dir")
  set(_dst_system_map "${BSIM_OUT_DIR}/system_map.xml")
  set(_dst_vbin "${BSIM_OUT_DIR}/${BSIM_PROJECT}_sim.vbin")
  set(_stamp "${BSIM_OUT_DIR}/.${BSIM_PROJECT}_sim.stamp")

  if(BSIM_USE_SYMLINK)
    set(_publish_vbin_cmd "${CMAKE_COMMAND}" -E create_symlink "${_src_vbin}" "${_dst_vbin}")
  else()
    set(_publish_vbin_cmd "${CMAKE_COMMAND}" -E copy_if_different "${_src_vbin}" "${_dst_vbin}")
  endif()

  set(_sim_args "--platform" "sim")
  if(NOT "${BSIM_SIM_TEMPLATE}" STREQUAL "")
    list(APPEND _sim_args "--sim-template" "${BSIM_SIM_TEMPLATE}")
  endif()
  if(NOT "${BSIM_SIM_MEM}" STREQUAL "")
    list(APPEND _sim_args "--sim-mem" "${BSIM_SIM_MEM}")
  endif()
  if(NOT "${BSIM_SIM_OUT}" STREQUAL "")
    list(APPEND _sim_args "--sim-out" "${BSIM_SIM_OUT}")
  endif()
  if(NOT "${BSIM_SYSTEM_MAP_OUT}" STREQUAL "")
    list(APPEND _sim_args "--system-map-out" "${BSIM_SYSTEM_MAP_OUT}")
  endif()

  set(_extra_deps "")
  foreach(dep IN ITEMS BSIM_SIM_TEMPLATE BSIM_SIM_MEM)
    if(NOT "${${dep}}" STREQUAL "")
      list(APPEND _extra_deps "${${dep}}")
    endif()
  endforeach()

  add_custom_command(
    OUTPUT "${_dst_vbin}" "${_stamp}"

    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BSIM_OUT_DIR}"

    # Run linker init (simulation platform)
    COMMAND "${_py}" "${_main_py}"
            init
            -p "${BSIM_PROJECT}"
            --cfg "${BSIM_CFG}"
            --kernels ${BSIM_KERNELS}
            ${_sim_args}

    # Build simulation project
    COMMAND "${_py}" "${_main_py}"
            build_hw_project
            -p "${BSIM_PROJECT}"
            --sim

    COMMAND "${CMAKE_COMMAND}" -E echo "Publishing SIM vbin:"
    COMMAND "${CMAKE_COMMAND}" -E echo "  from ${_src_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E echo "  to   ${_dst_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${_dst_vbin}"
    COMMAND ${_publish_vbin_cmd}

    COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"

    WORKING_DIRECTORY "${_main_dir}"

    DEPENDS
      "${_main_py}"
      "${BSIM_CFG}"
      ${BSIM_KERNELS}
      ${_extra_deps}

    COMMENT "SLASH SIM build: ${BSIM_PROJECT} -> ${BSIM_OUT_DIR}"
    VERBATIM
  )

  add_custom_target("${BSIM_TARGET}" DEPENDS "${_dst_vbin}")

  set_property(TARGET "${BSIM_TARGET}" PROPERTY SLASH_SIM_ROOT "${_sim_root}")
  set_property(TARGET "${BSIM_TARGET}" PROPERTY SLASH_SIM_VPP "${_src_vpp}")
  set_property(TARGET "${BSIM_TARGET}" PROPERTY SLASH_SIM_XSIM_DIR "${_src_xsim}")
  set_property(TARGET "${BSIM_TARGET}" PROPERTY SLASH_SYSTEM_MAP "${_src_system_map}")
  set_property(TARGET "${BSIM_TARGET}" PROPERTY SLASH_SIM_VBIN "${_dst_vbin}")
endfunction()
