if(NOT DEFINED INSTALL_DIR OR "${INSTALL_DIR}" STREQUAL "")
  set(INSTALL_DIR "/opt/amd/slash")
endif()

set(_required_files
  "abs_shell_service_layer.dcp"
  "abs_shell_slash.dcp"
  "amd_v80_gen5x8_25.1.pdi"
  "top_wrapper_routed_bb.dcp"
)

set(_missing "")
foreach(_f IN LISTS _required_files)
  if(NOT EXISTS "${INSTALL_DIR}/${_f}")
    list(APPEND _missing "${INSTALL_DIR}/${_f}")
  endif()
endforeach()

if(_missing)
  message(FATAL_ERROR "install was not run. ask your admin to run install first")
endif()
