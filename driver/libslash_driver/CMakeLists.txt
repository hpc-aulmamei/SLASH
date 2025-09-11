# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
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

cmake_minimum_required(VERSION 3.16)

project(libslash_driver
  VERSION 0.1.0
  LANGUAGES Cflatpak install flathub com.calibre_ebook.calibre
)

# Allow user to choose shared vs static (standard CMake variable)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
# Optionally build examples
option(SLASH_DRIVER_BUILD_EXAMPLES "Build example executables" ON)


include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(src)
if(SLASH_DRIVER_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# -------- Installation: headers and library --------
# Public headers are under include/  (layout: include/slash_driver/*.h)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  TARGETS slash_driver
  EXPORT  slash_driverTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# -------- CMake package configuration --------
# Generate the version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/slash_driverConfigVersion.cmake"
  VERSION       ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion
)

# Configure the main package config from template
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/slash_driverConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/slash_driverConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/slash_driver"
)

# Export targets for *install* tree
install(
  EXPORT slash_driverTargets
  NAMESPACE slash_driver::
  FILE slash_driverTargets.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/slash_driver"
)

# Install the config + version files
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/slash_driverConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/slash_driverConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/slash_driver"
)

# Export targets for *build* tree so a project can use this directory directly
export(
  EXPORT slash_driverTargets
  NAMESPACE slash_driver::
  FILE "${CMAKE_CURRENT_BINARY_DIR}/slash_driverTargets.cmake"
)
