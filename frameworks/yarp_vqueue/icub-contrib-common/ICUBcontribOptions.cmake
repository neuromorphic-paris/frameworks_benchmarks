# Copyright: (C) 2013 Istituto Italiano di Tecnologia
# Authors: Elena Ceseracciu
# CopyPolicy: Released under the terms of the GNU GPL v2.0.

include(GNUInstallDirs)
## ICUBCONTRIB_INSTALL_WITH_RPATH
if (NOT MSVC)
    option(ICUBCONTRIB_INSTALL_WITH_RPATH "Set an rpath after installing the executables" TRUE)
    if (ICUBCONTRIB_INSTALL_WITH_RPATH )
        set(CMAKE_SKIP_BUILD_RPATH  FALSE)
        # when building, don't use the install RPATH already
        # (but later on when installing), this tells cmake to relink
        # at install, so in-tree binaries have correct rpath
        set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

       # SET(CMAKE_INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
        set(CMAKE_INSTALL_RPATH "") #don't know if need this
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

        # the RPATH to be used when installing, but only if it's not a system directory
        list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
        if(${isSystemDir} EQUAL -1)
            set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
        endif()
    endif (ICUBCONTRIB_INSTALL_WITH_RPATH )

endif (NOT MSVC)

#########################################################################
# Compile libraries using -fPIC to produce position independent code
# since CMake 2.8.10 the variable CMAKE_POSITION_INDEPENDENT_CODE is
# used by cmake to determine whether position indipendent code
# executable and library targets should be created
# For older versions the position independent code is handled in
# iCubHelpers.cmake, in the icub_export_library macro (and obviously
# only for targets exported using that macro)

#if(NOT MSVC)
#    option(ICUBCONTRIB_PRODUCE_POSITION_INDEPENDENT_CODE "ompile libraries using -fPIC to produce position independent code" FALSE)
#    if(ICUBCONTRIB_PRODUCE_POSITION_INDEPENDENT_CODE)
#    if(CMAKE_VERSION VERSION_GREATER "2.8.9")
#        set(CMAKE_POSITION_INDEPENDENT_CODE "TRUE")
#    endif()
#endif()

##########################################################################
if(MSVC)
    set(CMAKE_DEBUG_POSTFIX "d")
endif(MSVC)

#########################################################################

if(NOT MSVC)
    option(ICUBCONTRIB_SHARED_LIBRARY "Compile shared libraries rather than static libraries" FALSE)
    if(ICUBCONTRIB_SHARED_LIBRARY)
        set(BUILD_SHARED_LIBS ON)
    endif()
endif()

#########################################################################

set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
message(STATUS "Libraries are placed in ${LIBRARY_OUTPUT_PATH}")
message(STATUS "Executables are placed in ${EXECUTABLE_OUTPUT_PATH}")

#########################################################################
# Handle CMAKE_CONFIGURATION_TYPES and CMAKE_BUILD_TYPE
set(ICUBCONTRIB_OPTIMIZED_CONFIGURATIONS "Release" "MinSizeRel")
set(ICUBCONTRIB_DEBUG_CONFIGURATIONS "Debug" "RelWithDebInfo")

if(NOT CMAKE_CONFIGURATION_TYPES)
    # Possible values for the CMAKE_BUILD_TYPE variable
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${ICUBCONTRIB_OPTIMIZED_CONFIGURATIONS} ${ICUBCONTRIB_DEBUG_CONFIGURATIONS})
    if(NOT CMAKE_BUILD_TYPE)
        # Encourage user to specify build type.
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    endif()
endif()

# Let CMake know which configurations are the debug ones, so that it can
# link the right library when both optimized and debug library are found
set_property(GLOBAL PROPERTY DEBUG_CONFIGURATIONS ${ICUBCONTRIB_DEBUG_CONFIGURATIONS})
