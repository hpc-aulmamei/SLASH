# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
<<<<<<< dev
# 
=======
#
>>>>>>> dev
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
<<<<<<< dev
# 
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
# 
=======
#
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
#
>>>>>>> dev
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

<<<<<<< dev

  set(_src_vbin "${BHW_LINKER_DIR}/results/${BHW_PROJECT}/${BHW_PROJECT}_hw.vbin")
  set(_dst_vbin "${BHW_OUT_DIR}/${BHW_PROJECT}_hw.vbin")
  set(_stamp    "${BHW_OUT_DIR}/.${BHW_PROJECT}_linker.stamp")
=======
  if(DEFINED VIVADO_BINARY AND NOT "${VIVADO_BINARY}" STREQUAL "")
    set(_vivado "${VIVADO_BINARY}")
  else()
    set(_vivado "vivado")
  endif()

  set(_check_install_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckSlashInstall.cmake")
  set(_slash_rm_tcl "${BHW_LINKER_DIR}/resources/base/scripts/slash_project_build.tcl")
  set(_service_rm_tcl "${BHW_LINKER_DIR}/resources/base/scripts/service_layer_build.tcl")

  foreach(req_script "${_check_install_script}" "${_slash_rm_tcl}" "${_service_rm_tcl}")
    if(NOT EXISTS "${req_script}")
      message(FATAL_ERROR "build_hw(): required script not found: '${req_script}'")
    endif()
  endforeach()

  set(_linker_results_dir "${BHW_LINKER_DIR}/results")
  set(_linker_info "${_linker_results_dir}/${BHW_PROJECT}/.linker_info.json")
  set(_slash_bd_tcl "${_linker_results_dir}/${BHW_PROJECT}/bd/slash_${BHW_PROJECT}.tcl")
  set(_service_bd_tcl "${_linker_results_dir}/${BHW_PROJECT}/bd/service_layer_${BHW_PROJECT}.tcl")

  set(_hw_work_root "${BHW_OUT_DIR}/${BHW_PROJECT}_hw_build")
  set(_stamps_dir "${_hw_work_root}/stamps")
  set(_logs_dir "${_hw_work_root}/logs")
  set(_reports_dir "${_hw_work_root}/reports")
  set(_rm_service_work_dir "${_hw_work_root}/rm/service_layer_${BHW_PROJECT}")
  set(_rm_slash_work_dir "${_hw_work_root}/rm/slash_${BHW_PROJECT}")
  set(_rm_artifact_dir "${_hw_work_root}/slash.runs/${BHW_PROJECT}_impl_1")
  set(_raw_util_report "${_reports_dir}/report_utilization_${BHW_PROJECT}.txt")
  set(_linker_util_report "${_linker_results_dir}/${BHW_PROJECT}/report_utilization_${BHW_PROJECT}.txt")

  set(_service_partial_pdi "${_rm_artifact_dir}/top_i_service_layer_service_layer_${BHW_PROJECT}_inst_0_partial.pdi")
  set(_slash_partial_pdi "${_rm_artifact_dir}/top_i_slash_slash_${BHW_PROJECT}_inst_0_partial.pdi")

  set(_check_install_stamp "${_stamps_dir}/check_install.stamp")
  set(_linker_init_stamp "${_stamps_dir}/linker_init.stamp")
  set(_linker_generate_tcl_stamp "${_stamps_dir}/linker_generate_tcl.stamp")
  set(_service_rm_stamp "${_stamps_dir}/service_layer_rm_build.stamp")
  set(_slash_rm_stamp "${_stamps_dir}/slash_rm_build.stamp")
  set(_publish_util_stamp "${_stamps_dir}/publish_util_report.stamp")
  set(_linker_complete_hw_build_stamp "${_stamps_dir}/linker_complete_hw_build.stamp")
  set(_linker_create_metadata_stamp "${_stamps_dir}/linker_create_metadata.stamp")

  set(_src_vbin "${_linker_results_dir}/${BHW_PROJECT}/${BHW_PROJECT}_hw.vbin")
  set(_dst_vbin "${BHW_OUT_DIR}/${BHW_PROJECT}_hw.vbin")
  set(_final_publish_stamp "${_stamps_dir}/publish_vbin.stamp")
>>>>>>> dev

  if(BHW_USE_SYMLINK)
    set(_publish_cmd "${CMAKE_COMMAND}" -E create_symlink "${_src_vbin}" "${_dst_vbin}")
  else()
    set(_publish_cmd "${CMAKE_COMMAND}" -E copy_if_different "${_src_vbin}" "${_dst_vbin}")
  endif()

<<<<<<< dev
  add_custom_command(
    OUTPUT "${_dst_vbin}" "${_stamp}"

    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BHW_OUT_DIR}"

    # Run linker
    COMMAND "${_py}" "${_main_py}"
=======
  set(_env_hw "${CMAKE_COMMAND}" -E env "SLASH_HW_BUILD_DIR=${_hw_work_root}")
  set(_service_rm_extra_args "")
  if(DEFINED EN_SERVICE_LAYER AND EN_SERVICE_LAYER)
    list(APPEND _service_rm_extra_args --force)
  endif()

  # [1/9] Build-time install preflight.
  add_custom_command(
    OUTPUT "${_check_install_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}"
    COMMAND "${CMAKE_COMMAND}" -D "INSTALL_DIR=/opt/amd/slash" -P "${_check_install_script}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_check_install_stamp}"
    DEPENDS "${_check_install_script}"
    COMMENT "SLASH HW [1/9]: check /opt/amd/slash install artifacts"
    VERBATIM
  )

  # [2/9] Initialize linker state.
  add_custom_command(
    OUTPUT "${_linker_init_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BHW_OUT_DIR}" "${_hw_work_root}" "${_stamps_dir}" "${_logs_dir}" "${_reports_dir}" "${_rm_artifact_dir}"
    COMMAND ${_env_hw} "${_py}" "${_main_py}"
            init
>>>>>>> dev
            -p "${BHW_PROJECT}"
            --cfg "${BHW_CFG}"
            --kernels ${BHW_KERNELS}
            --ip-repository "${BHW_IP_REPO}"
<<<<<<< dev

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
=======
    COMMAND "${CMAKE_COMMAND}" -E touch "${_linker_init_stamp}"
    WORKING_DIRECTORY "${_main_dir}"
    DEPENDS "${_check_install_stamp}" "${_main_py}" "${BHW_CFG}" ${BHW_KERNELS}
    COMMENT "SLASH HW [2/9]: linker init"
    VERBATIM
  )

  # [3/9] Generate linker Tcl.
  add_custom_command(
    OUTPUT "${_linker_generate_tcl_stamp}"
    BYPRODUCTS "${_slash_bd_tcl}" "${_service_bd_tcl}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}"
    COMMAND ${_env_hw} "${_py}" "${_main_py}"
            generate_tcl
            -p "${BHW_PROJECT}"
            --linker-info "${_linker_info}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_linker_generate_tcl_stamp}"
    WORKING_DIRECTORY "${_main_dir}"
    DEPENDS "${_linker_init_stamp}" "${_main_py}" "${BHW_CFG}" ${BHW_KERNELS}
    COMMENT "SLASH HW [3/9]: linker generate_tcl"
    VERBATIM
  )

  # [4/9] Build service-layer reconfigurable module.
  add_custom_command(
    OUTPUT "${_service_rm_stamp}"
    BYPRODUCTS "${_service_partial_pdi}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}" "${_logs_dir}" "${_rm_service_work_dir}" "${_rm_artifact_dir}"
    COMMAND ${_env_hw} "${_py}" "${_main_py}"
            build_service_layer_rm
            -p "${BHW_PROJECT}"
            --linker-info "${_linker_info}"
            --install-dir "/opt/amd/slash"
            --vivado-bin "${_vivado}"
            --jobs 8
            ${_service_rm_extra_args}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_service_rm_stamp}"
    WORKING_DIRECTORY "${_hw_work_root}"
    DEPENDS "${_linker_generate_tcl_stamp}" "${_service_rm_tcl}"
    COMMENT "SLASH HW [4/9]: service_layer RM build"
    VERBATIM
  )

  # [5/9] Build slash reconfigurable module and utilization report.
  add_custom_command(
    OUTPUT "${_slash_rm_stamp}"
    BYPRODUCTS "${_slash_partial_pdi}" "${_raw_util_report}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}" "${_logs_dir}" "${_rm_slash_work_dir}" "${_rm_artifact_dir}" "${_reports_dir}"
    COMMAND ${_env_hw} "${_py}" "${_main_py}"
            build_slash_rm
            -p "${BHW_PROJECT}"
            --linker-info "${_linker_info}"
            --install-dir "/opt/amd/slash"
            --vivado-bin "${_vivado}"
            --jobs 8
    COMMAND "${CMAKE_COMMAND}" -E touch "${_slash_rm_stamp}"
    WORKING_DIRECTORY "${_hw_work_root}"
    DEPENDS "${_linker_generate_tcl_stamp}" "${_slash_rm_tcl}"
    COMMENT "SLASH HW [5/9]: slash RM build"
    VERBATIM
  )

  # [6/9] Publish utilization report to linker/results for XML conversion compatibility.
  get_filename_component(_linker_util_dir "${_linker_util_report}" DIRECTORY)
  add_custom_command(
    OUTPUT "${_publish_util_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}" "${_linker_util_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_raw_util_report}" "${_linker_util_report}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_publish_util_stamp}"
    DEPENDS "${_slash_rm_stamp}"
    COMMENT "SLASH HW [6/9]: publish utilization report"
    VERBATIM
  )

  # [7/9] Advance linker state after external RM builds.
  add_custom_command(
    OUTPUT "${_linker_complete_hw_build_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}"
    COMMAND ${_env_hw} "${_py}" "${_main_py}"
            complete_hw_build
            -p "${BHW_PROJECT}"
            --linker-info "${_linker_info}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_linker_complete_hw_build_stamp}"
    WORKING_DIRECTORY "${_main_dir}"
    DEPENDS "${_service_rm_stamp}" "${_publish_util_stamp}" "${_main_py}"
    COMMENT "SLASH HW [7/9]: linker complete_hw_build"
    VERBATIM
  )

  # [8/9] Run linker metadata generation (images/report XML/vbin).
  add_custom_command(
    OUTPUT "${_linker_create_metadata_stamp}"
    BYPRODUCTS "${_src_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stamps_dir}"
    COMMAND ${_env_hw} "${_py}" "${_main_py}"
            create_metadata
            -p "${BHW_PROJECT}"
            --linker-info "${_linker_info}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_linker_create_metadata_stamp}"
    WORKING_DIRECTORY "${_main_dir}"
    DEPENDS "${_linker_complete_hw_build_stamp}" "${_main_py}"
    COMMENT "SLASH HW [8/9]: linker create_metadata"
    VERBATIM
  )

  # [9/9] Publish final HW vbin to the requested output directory.
  add_custom_command(
    OUTPUT "${_dst_vbin}" "${_final_publish_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BHW_OUT_DIR}" "${_stamps_dir}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Publishing HW vbin:"
    COMMAND "${CMAKE_COMMAND}" -E echo "  from ${_src_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E echo "  to   ${_dst_vbin}"
    COMMAND "${CMAKE_COMMAND}" -E remove -f "${_dst_vbin}"
    COMMAND ${_publish_cmd}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_final_publish_stamp}"
    DEPENDS "${_linker_create_metadata_stamp}"
    COMMENT "SLASH HW [9/9]: publish ${BHW_PROJECT}_hw.vbin"
>>>>>>> dev
    VERBATIM
  )

  add_custom_target("${BHW_TARGET}" DEPENDS "${_dst_vbin}")

  set_property(TARGET "${BHW_TARGET}" PROPERTY SLASH_VBIN "${_dst_vbin}")
  set_property(TARGET "${BHW_TARGET}" PROPERTY SLASH_VBIN_SRC "${_src_vbin}")
endfunction()
