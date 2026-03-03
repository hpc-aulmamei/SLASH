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

function(resolve_slash_linker_results_dir out_var)
  if(DEFINED ENV{SLASH_LINKER_RESULTS_DIR})
    set(_env_results "$ENV{SLASH_LINKER_RESULTS_DIR}")
    string(STRIP "${_env_results}" _env_results)
    if("${_env_results}" STREQUAL "")
      message(FATAL_ERROR
        "SLASH_LINKER_RESULTS_DIR is set but empty. Set it to an absolute directory path."
      )
    endif()
    if(NOT IS_ABSOLUTE "${_env_results}")
      message(FATAL_ERROR
        "SLASH_LINKER_RESULTS_DIR must be an absolute path, got: '${_env_results}'"
      )
    endif()
    set(_resolved "${_env_results}")
  elseif(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
    set(_resolved "$ENV{XDG_CACHE_HOME}/slash/linker_results")
  elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    set(_resolved "$ENV{HOME}/.cache/slash/linker_results")
  else()
    set(_resolved "/home/user/.cache/slash/linker_results")
  endif()

  file(TO_CMAKE_PATH "${_resolved}" _resolved_norm)
  set(${out_var} "${_resolved_norm}" PARENT_SCOPE)
endfunction()
