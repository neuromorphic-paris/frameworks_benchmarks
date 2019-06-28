cmake_minimum_required(VERSION 2.8)

project(benchmark)
cmake_policy(SET CMP0004 OLD)
cmake_policy(SET CMP0007 OLD)
cmake_policy(SET CMP0022 NEW)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

# Offer the user the choice of overriding the installation directories
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")
set(INSTALL_SUPPORT_DIR "share/kAER" CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDE_DIR include/libkAER CACHE PATH "Installation directory for header files")
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WIN64 TRUE)
    endif()
endif()

set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Installation directory for CMake files")
set(CMAKE_CXX_FLAGS "-std=c++11 ${BASE_FLAGS} ${GPROF_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

SET(common_libraries "")
find_package(Boost COMPONENTS system chrono python thread REQUIRED)
find_package(PythonLibs 2.7 EXACT)
find_package(PythonLibs)
include_directories(${PYTHON_INCLUDE_DIRS})
link_directories(${PYTHON_LIBRARIES})
set(common_libraries ${common_libraries} ${PYTHON_LIBRARIES})
set(common_libraries ${common_libraries} ${Boost_LIBRARIES})
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
find_package(libatis REQUIRED)
include_directories(${LIBATIS_INCLUDE_DIRS})
find_package(libkaer REQUIRED)
include_directories(${LIBKAER_INCLUDE_DIRS})

MESSAGE(STATUS ${LIBKAER_LIBRARIES})

set(benchmark_apps
    mask
    mask_latencies
    flow
    flow_latencies
    denoised_flow
    denoised_flow_latencies
    masked_denoised_flow
    masked_denoised_flow_latencies
    masked_denoised_flow_activity
    masked_denoised_flow_activity_latencies)
FOREACH(app ${benchmark_apps})
    add_executable(${app} source/${app}.cpp)
    target_link_libraries(${app} ${LIBKAER_LIBRARIES} ${LIBATIS_LIBRARIES} ${common_libraries})
ENDFOREACH()
