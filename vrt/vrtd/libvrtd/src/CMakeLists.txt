add_library(
    libvrtd

    ${CMAKE_CURRENT_SOURCE_DIR}/requests.c
)

target_include_directories(libvrtd
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libvrtd/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# CMake prepends lib for libraries so libvrtd would be output as liblibvrtd otherwise
set_target_properties(libvrtd PROPERTIES OUTPUT_NAME vrtd)

