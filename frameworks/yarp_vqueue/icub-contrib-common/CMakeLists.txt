# Copyright: (C) 2013 Istituto Italiano di Tecnologia
# Authors: Elena Ceseracciu
# CopyPolicy: Released under the terms of the GNU GPL v2.0.

cmake_minimum_required(VERSION 3.5)
project(ICUBcontrib NONE)

find_package(YARP 3.0.0 REQUIRED)
set(YARP_FORCE_DYNAMIC_PLUGINS TRUE CACHE INTERNAL "ICUBcontrib is always built with dynamic plugins")

yarp_configure_external_installation(ICUBcontrib WITH_PLUGINS)
set(ICUBCONTRIB_MODULE_DIR share/${PROJECT_NAME}/cmake)

configure_file(${CMAKE_SOURCE_DIR}/templates/ICUBcontribConfig.cmake.in
               ${CMAKE_BINARY_DIR}/ICUBcontribConfig.cmake.build @ONLY)

install(FILES ${CMAKE_BINARY_DIR}/ICUBcontribConfig.cmake.build DESTINATION lib/${PROJECT_NAME} RENAME ICUBcontribConfig.cmake)
install(FILES ICUBcontribHelpers.cmake ICUBcontribOptions.cmake DESTINATION ${ICUBCONTRIB_MODULE_DIR})
install(FILES templates/icubcontrib-config-install.cmake.in templates/icubcontrib-config-uninstall.cmake.in DESTINATION ${ICUBCONTRIB_MODULE_DIR}/templates)
