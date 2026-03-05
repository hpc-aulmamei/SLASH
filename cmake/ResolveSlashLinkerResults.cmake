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

function(resolve_slash_linker_results_dir out_var base_dir project_name)
  if("${base_dir}" STREQUAL "")
    message(FATAL_ERROR "resolve_slash_linker_results_dir(): base_dir is required")
  endif()
  if("${project_name}" STREQUAL "")
    message(FATAL_ERROR "resolve_slash_linker_results_dir(): project_name is required")
  endif()

  string(REGEX REPLACE "[^A-Za-z0-9_]+" "_" _project_sanitized "${project_name}")
  if("${_project_sanitized}" STREQUAL "")
    set(_project_sanitized "proj")
  endif()
  string(SUBSTRING "${_project_sanitized}" 0 1 _first_char)
  if(_first_char MATCHES "^[0-9]$")
    set(_project_sanitized "_${_project_sanitized}")
  endif()

  get_filename_component(_base_abs "${base_dir}" ABSOLUTE)
  file(TO_CMAKE_PATH "${_base_abs}" _base_norm)
  set(_resolved "${_base_norm}/linker_results_${_project_sanitized}")
  set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

function(resolve_slash_linker_platform_results_dir out_var base_dir project_name platform)
  resolve_slash_linker_results_dir(_results_root "${base_dir}" "${project_name}")
  string(TOLOWER "${platform}" _platform_norm)
  if(NOT "${_platform_norm}" MATCHES "^(hw|sim|emu)$")
    message(FATAL_ERROR
      "resolve_slash_linker_platform_results_dir(): unsupported platform '${platform}' "
      "(expected: hw, sim, emu)"
    )
  endif()
  set(${out_var} "${_results_root}/${_platform_norm}" PARENT_SCOPE)
endfunction()
