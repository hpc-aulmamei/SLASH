add_library(
    libvrtdpp

    ${CMAKE_CURRENT_SOURCE_DIR}/bar_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/error.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session.cpp
)

target_include_directories(libvrtdpp
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libvrtd/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libvrtdpp/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# CMake prepends lib for libraries so libvrtdpp would be output as liblibvrtdpp otherwise
set_target_properties(libvrtdpp PROPERTIES OUTPUT_NAME vrtdpp)

set_target_properties(libvrtdpp PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS YES
)

target_link_libraries(libvrtdpp PUBLIC libvrtd)

