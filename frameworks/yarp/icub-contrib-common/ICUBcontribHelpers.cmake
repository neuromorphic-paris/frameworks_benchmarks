# Copyright: (C) 2013 Istituto Italiano di Tecnologia
# Authors: Elena Ceseracciu
# CopyPolicy: Released under the terms of the GNU GPL v2.0.

# Modules in iCub/contrib should call this macro, so that the default CMake installation prefix is common for all
# modules, and this simplifies environment configuration. A warning is issued to the user when the installation prefix
# in the CMake cache is different from the one chosen when configuring the ICUBcontrib package that this macro belongs to.

macro(icubcontrib_set_default_prefix)
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
      set(CMAKE_INSTALL_PREFIX
         ${ICUBCONTRIB_INSTALL_PREFIX} CACHE PATH "Install prefix" FORCE
         )
    endif(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if (NOT CMAKE_INSTALL_PREFIX STREQUAL ICUBCONTRIB_INSTALL_PREFIX)
      message(WARNING "Installation prefix is different from the ICUBcontrib one, which is ${ICUBCONTRIB_INSTALL_PREFIX}.")
    endif()
endmacro()

##########################################################

include(CMakeParseArguments)
include(GNUInstallDirs)

# Export library function. Export a target to be used from external programs
#
# icubcontrib_export_library(target
#                       [INTERNAL_INCLUDE_DIRS dir1 dir2 ...]
#                       [EXTERNAL_INCLUDE_DIRS dir1 dir2 ...]
#                       [DEPENDS target1 target2 ...]
#                       [DESTINATION dest]
#                       [VERBOSE]
#                       [FILES file1 file2 ...]
#                       [FILES_WITH_PATH file1 file2 ...])
# - target: target name
# - INTERNAL_INCLUDE_DIRS a list of directories that contain header files when building in-source
# - EXTERNAL_INCLUDE_DIRS a list of directories that contain header files external to the repository
# - DEPENDS a list of dependencies; these are targets built within the repository. Important CMake should
#   parse these targets *before* the current target (check sub_directories(...)).
# - VERBOSE: ask to print parameters (for debugging)
# - DESTINATION: destination directory to which header files will be copied (relative w.r.t. install prefix)
# - FILES: a list of files that will be copied to destination (header files)
# - FILES_WITH_PATH: a list of files that will be copied to destination, keeping the relative path (header files)
#
#  The function does a bunch of things:
#
# -append ${target} to the list of targets built within the project (global property ICUBCONTRIB_TARGETS)
# -retrieve INTERNAL_INCLUDE_DIRS/EXTERNAL_INCLUDE_DIRS properties for each dependency
# -build INTERNAL_INCLUDE_DIRS by merging INTERNAL_INCLUDE_DIRS and the property INTERNAL_INCLUDE_DIRS of each
#  dependency target -- store it as a property for the current target
# -creates a DEPENDS property for the target, this contains the list of dependencies
# -similarly as above for EXTERNAL_INCLUDE_DIRS
# -merge EXTERNAL/INTERNAL_INCLUDE_DIRS into INCLUDE_DIRS for the current target, store it as property and cache
#  variable
# -set up install rule for copying all FILES and FILES_WITH_HEADER to DESTINATION
# 
#
# Note: this function has to be called by all targets.
# Input arguments are consistent with the icub_export_library macro in the iCub/main repository.
# The behaviour of the two macros is different in that the "export to build tree" case is not considered 
# (support for this can be easily added to the icubcontrib_finalize_export macro), and static libraries are not compiled with
# PIC flag (for position independent code generation) with gcc.

macro(icubcontrib_export_library target)
  cmake_parse_arguments(${target} "VERBOSE" "DESTINATION" "INTERNAL_INCLUDE_DIRS;EXTERNAL_INCLUDE_DIRS;DEPENDS;FILES;FILES_WITH_PATH;PATH_TO_EXCLUDE" ${ARGN})

  set(VERBOSE ${${target}_VERBOSE})
  if(VERBOSE)
    MESSAGE(STATUS "*** Arguments for ${target}")
    MESSAGE(STATUS "Internal directories: ${${target}_INTERNAL_INCLUDE_DIRS}")
    MESSAGE(STATUS "External directories: ${${target}_EXTERNAL_INCLUDE_DIRS}")
    MESSAGE(STATUS "Dependencies: ${${target}_DEPENDS}")
    MESSAGE(STATUS "Destination: ${${target}_DESTINATION}")
    MESSAGE(STATUS "Header files: ${${target}_FILES}")
    MESSAGE(STATUS "Header files for which we keep the relative path: ${${target}_FILES_WITH_PATH}")
    MESSAGE(STATUS "Part of the relative path to strip off: ${${target}_PATH_TO_EXCLUDE}")
    MESSAGE(STATUS "Option verbosity: ${${target}_VERBOSE}")
  endif()

  set(internal_includes ${${target}_INTERNAL_INCLUDE_DIRS})
  set(external_includes ${${target}_EXTERNAL_INCLUDE_DIRS})
  set(dependencies ${${target}_DEPENDS})
  set(files ${${target}_FILES})
  set(files_with_path ${${target}_FILES_WITH_PATH})
  set(path_to_exclude ${${target}_PATH_TO_EXCLUDE})
  set(destination ${${target}_DESTINATION})

  ##### Append target to global list.
  set_property(GLOBAL APPEND PROPERTY ICUBCONTRIB_TARGETS ${target})
  # Install/export rules
  install(TARGETS ${target} EXPORT icubcontrib-targets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development)
  if (MSVC) 
    install (FILES ${CMAKE_BINARY_DIR}/lib/Debug/${target}d.pdb 
                    DESTINATION ${CMAKE_INSTALL_LIBDIR} 
                    CONFIGURATIONS Debug 
                    COMPONENT Development
                    OPTIONAL)
  endif()
#TODO: move to "final" macro
 # install(EXPORT ${target}-targets DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} FILE ${EXPORT_CONFIG_FILE} COMPONENT Development) #TODO check: use project_name, or ICUBcontrib?
# Export to build directory is disabled:
#  export(TARGETS ${target} FILE ${CMAKE_BINARY_DIR}/${target}-targets-build)
#important wrap ${dependencies} with "" to allows storing a list of dependencies
  set_target_properties(${target} PROPERTIES DEPENDS "${dependencies}")

##### Handle include directories
# Parsing dependencies
  if (dependencies)
    foreach (d ${dependencies})
        get_target_property(in_dirs ${d} INTERNAL_INCLUDE_DIRS)
        get_target_property(ext_dirs ${d} EXTERNAL_INCLUDE_DIRS)

        if (VERBOSE)
            message(STATUS "Getting from target ${d}:")
            message(STATUS "${in_dirs}")
            message(STATUS "${ext_dirs}")
        endif()

        if (in_dirs)
            set(internal_includes ${internal_includes} ${in_dirs})
        endif (in_dirs)

        if (ext_dirs)
            set(external_includes ${external_includes} ${ext_dirs})
        endif(ext_dirs)
    endforeach(d)
  endif(dependencies)
  ############################

  ################ Build unique variable with internal and external include directories
  ## Set corresponding target's properties
  set(include_dirs "")

  if (internal_includes)
    list(REMOVE_DUPLICATES internal_includes)
    set_target_properties(${target} PROPERTIES
                        INTERNAL_INCLUDE_DIRS
                        "${internal_includes}")
    if(VERBOSE)
        message(STATUS "Target ${target} exporting internal headers: ${internal_includes}")
    endif()
    list(APPEND include_dirs ${internal_includes})
  endif()

  if (external_includes)
    list(REMOVE_DUPLICATES external_includes)
    set_target_properties(${target} PROPERTIES
                        EXTERNAL_INCLUDE_DIRS
                        "${external_includes}")
    if(VERBOSE)
        message(STATUS "Target ${target} exporting external headers: ${external_includes}")
    endif()
    list(APPEND include_dirs ${external_includes})
  endif()

  if (include_dirs)
    list(REMOVE_DUPLICATES include_dirs)
    set_property(TARGET ${target} PROPERTY INCLUDE_DIRS  "${include_dirs}")
    if (VERBOSE)
        message(STATUS "Target ${target} exporting: ${include_dirs}")
    endif()
    set(${target}_INCLUDE_DIRS "${include_dirs}" CACHE STRING "include directories for target ${target}" FORCE)
  endif()

  ##############################################

  # Compile libraries using -fPIC to produce position independent code
  # For CMAKE_VERSION >= 2.8.10 this is handled in iCubOptions.cmake
  # using the CMAKE_POSITION_INDEPENDENT_CODE flag
#TODO insert option to enable PIC compilation
#  if(CMAKE_COMPILER_IS_GNUCXX AND NOT BUILD_SHARED_LIBS)
#    if(CMAKE_VERSION VERSION_EQUAL "2.8.9")
#      set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
#    elseif(CMAKE_VERSION VERSION_LESS "2.8.9")
#      set_target_properties(${target} PROPERTIES COMPILE_FLAGS -fPIC)
#    endif()
#  endif()



  #### Files export rules
  if (files AND destination)
    if (VERBOSE)
        message(STATUS "Target ${target} installing ${files} to ${destination}")
    endif()
    install(FILES ${files} DESTINATION ${destination} COMPONENT Development)

    set_target_properties(${target} PROPERTIES
                        HEADERFILES
                        "${files}")

    set_target_properties(${target} PROPERTIES
                        HEADERS_DESTINATION
                        ${destination})
  endif()

  #### Files export rules for files_with_path case
  if (files_with_path AND destination)
    if (VERBOSE)
        message(STATUS "Target ${target} installing ${files_with_path} to ${destination}")
    endif()
   

    if(path_to_exclude)
        # strip off the trailing slash
        string(REGEX REPLACE "/+$" "" path_to_exclude ${path_to_exclude})
    endif()

    foreach(cur_file  ${files_with_path})
        get_filename_component(file_rel_dir ${cur_file} PATH)
        if(path_to_exclude)
            string(REPLACE "${path_to_exclude}" "" file_rel_dir ${file_rel_dir})
        endif()
        install(FILES ${cur_file} DESTINATION ${destination}/${file_rel_dir} COMPONENT Development)
    endforeach()
    set_target_properties(${target} PROPERTIES
                        HEADERFILES_WITH_PATH
                        "${files_with_path}")

    set_target_properties(${target} PROPERTIES
                        HEADERS_DESTINATION
                        ${destination})
                        
  endif()

endmacro(icubcontrib_export_library)

##########################################################
# Create and install CMake files that allow all targets added with the icubcontrib_export_library macro
# to be imported by other projects.
#
# icub_contrib_finalize_export (packagename)
#
# will allow to import targets from this project with a find_package(packagename) call.
# It is suggested to set the CMAKE_PREFIX_PATH environment variable to point to the installation directory
# where all iCub-contrib modules should be installed (see icubcontrib_set_default_prefix macro).

macro(icubcontrib_finalize_export packagename)

set(packagename ${packagename})
#### prepare config file for installation
get_property(MY_ICUBCONTRIB_TARGETS GLOBAL PROPERTY ICUBCONTRIB_TARGETS)
set(EXPORT_CONFIG_FILE "${packagename}-export-install.cmake")
set(EXPORT_INCLUDE_FILE "${packagename}-export-install-includes.cmake")

set(INSTALL_CONFIG_FILE "${packagename}-config-for-install.cmake")
set(INSTALL_CONFIG_TEMPLATE "${ICUBCONTRIB_MODULE_DIR}/templates/icubcontrib-config-install.cmake.in")

file(WRITE ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "")
file(APPEND ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "# This file is automatically generated, see conf/iCubExportForInstall.cmake\n")
file(APPEND ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "###################\n")
file(APPEND ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "# List of include directories for exported targets\n\n")

set(include_dirs "")
foreach (t ${MY_ICUBCONTRIB_TARGETS})
  get_property(target_INCLUDE_DIRS TARGET ${t} PROPERTY EXTERNAL_INCLUDE_DIRS)
  set(include_dirs ${include_dirs} ${target_INCLUDE_DIRS}) 

  if (target_INCLUDE_DIRS)
      file(APPEND ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "set(${t}_INCLUDE_DIRS \"${CMAKE_INSTALL_PREFIX}/include\" \"${target_INCLUDE_DIRS}\" CACHE STRING \"include dir for target ${t}\")\n")
  else()
      file(APPEND ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "set(${t}_INCLUDE_DIRS \"${CMAKE_INSTALL_PREFIX}/include\" CACHE STRING \"include dir for target ${t}\")\n")
 endif()
endforeach(t)

if(include_dirs)
   LIST(REMOVE_DUPLICATES include_dirs)
endif(include_dirs)

#set(${packagename}_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include ${include_dirs})
file(APPEND ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} "set(${packagename}_INCLUDE_DIRS \"${CMAKE_INSTALL_PREFIX}/include\" \"${include_dirs}\" CACHE STRING \"include dir for target ${t}\")\n\n")

install(EXPORT icubcontrib-targets DESTINATION "${CMAKE_INSTALL_LIBDIR}/${packagename}" FILE ${EXPORT_CONFIG_FILE} COMPONENT Development)

CONFIGURE_FILE(${INSTALL_CONFIG_TEMPLATE}
  ${CMAKE_BINARY_DIR}/${INSTALL_CONFIG_FILE} @ONLY)

install(FILES ${CMAKE_BINARY_DIR}/${INSTALL_CONFIG_FILE} DESTINATION "${CMAKE_INSTALL_LIBDIR}/${packagename}" RENAME ${packagename}Config.cmake COMPONENT Development)
install(FILES ${CMAKE_BINARY_DIR}/${EXPORT_INCLUDE_FILE} DESTINATION "${CMAKE_INSTALL_LIBDIR}/${packagename}" COMPONENT Development)

endmacro(icubcontrib_finalize_export)

##########################################################
# Add "uninstall" target to the project (macro adapted from the CMake wiki)

macro(icubcontrib_add_uninstall_target)
## add the "uninstall" target
configure_file(
  "${ICUBCONTRIB_MODULE_DIR}/templates/icubcontrib-config-uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/icubcontrib-config-uninstall.cmake" @ONLY)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/icubcontrib-config-uninstall.cmake")
endmacro(icubcontrib_add_uninstall_target)
