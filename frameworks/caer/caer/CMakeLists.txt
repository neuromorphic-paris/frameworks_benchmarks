# Init CMake (require at least version 2.8.12)
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)

# General build settings
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE "Release" CACHE STRING "Possible build types: None Debug Release RelWithDebInfo MinSizeRel")
ENDIF()

# Project name and version
PROJECT(cAER C CXX)
SET(PROJECT_VERSION_MAJOR 1)
SET(PROJECT_VERSION_MINOR 2)
SET(PROJECT_VERSION_PATCH 1)
SET(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Compiler cache support
FIND_PROGRAM(CCACHE_FOUND ccache)
IF (CCACHE_FOUND)
	SET_PROPERTY(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
	SET_PROPERTY(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
ENDIF()

# Define installation paths
INCLUDE(GNUInstallDirs)

# Custom CMake base module
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/caer /usr/${CMAKE_INSTALL_DATAROOTDIR}/caer /usr/local/${CMAKE_INSTALL_DATAROOTDIR}/caer
  ${CMAKE_INSTALL_PREFIX}/share/caer /usr/share/caer /usr/local/share/caer)

# Basic setup for cAER
INCLUDE(caer-base)
CAER_SETUP(TRUE)

# Search for external libraries with pkg-config.
INCLUDE(FindPkgConfig)

# Basic device and threads support.
SET(CAER_INCDIRS "${CAER_LOCAL_INCDIRS}")
SET(CAER_LIBDIRS "${CAER_LOCAL_LIBDIRS}")

PKG_CHECK_MODULES(LIBCAER REQUIRED libcaer=3.1.0)
SET(CAER_PKGCONFIG_REQUIRES_PRIVATE "libcaer = 3.1.0")

SET(CAER_INCDIRS ${CAER_INCDIRS} ${LIBCAER_INCLUDE_DIRS})
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${LIBCAER_LIBRARY_DIRS})
SET(CAER_LIBS ${LIBCAER_LIBRARIES} ${CAER_C_THREAD_LIBS})

# Boost support for C++
FIND_PACKAGE(Boost 1.50 REQUIRED COMPONENTS system filesystem iostreams program_options)

SET(CAER_INCDIRS ${CAER_INCDIRS} ${Boost_INCLUDE_DIRS})
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${Boost_LIBRARY_DIRS})
SET(CAER_LIBS ${CAER_LIBS} ${Boost_LIBRARIES})

IF ((NOT (Boost_MAJOR_VERSION EQUAL 1 AND Boost_MINOR_VERSION GREATER_EQUAL 61))
	OR (OS_UNIX OR OS_LINUX OR OS_MACOSX))
	# libdl needed if Boost too old to have Boost.DLL as a replacement,
	# or for Boost.DLL itself on some platforms like Linux and MacOS X.
	SET(CAER_LIBS ${CAER_LIBS} dl)
ENDIF()

# Windows boost.ASIO needs extra libraries
IF (OS_WINDOWS)
	SET(CAER_LIBS ${CAER_LIBS} wsock32 ws2_32)
ENDIF()

# Add local directory to include and library paths
SET(CAER_INCDIRS ${CAER_INCDIRS} ${CMAKE_SOURCE_DIR}/)
SET(CAER_LIBDIRS ${CAER_LIBDIRS} ${CMAKE_SOURCE_DIR}/)

# Set include/library paths for successive compilation.
INCLUDE_DIRECTORIES(${CAER_INCDIRS})
LINK_DIRECTORIES(${CAER_LIBDIRS})

MESSAGE(STATUS "Include directories: ${CAER_INCDIRS}")
MESSAGE(STATUS "Link directories: ${CAER_LIBDIRS}")

# Install SDK library.
ADD_SUBDIRECTORY(caer-sdk)

# Compile libcaersdk and caer-bin main executable.
ADD_SUBDIRECTORY(src)

# Compile extra modules and utilities.
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(utils)

# Generate pkg-config file
CONFIGURE_FILE(libcaersdk.pc.in libcaersdk.pc @ONLY)

INSTALL(FILES ${CMAKE_BINARY_DIR}/libcaersdk.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
