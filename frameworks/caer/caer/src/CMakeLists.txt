# SDK support library.
SET(LIBCAERSDK_SRC_FILES
	module_sdk.cpp
	mainloop_sdk.cpp
	portability_sdk.cpp
	sshs/sshs.cpp
	sshs/sshs_helper.cpp
	sshs/sshs_node.cpp)

# Set full RPATH
SET(CMAKE_INSTALL_RPATH ${CAER_LOCAL_PREFIX}/${CMAKE_INSTALL_LIBDIR})

# Compile SDK library.
ADD_LIBRARY(caersdk SHARED ${LIBCAERSDK_SRC_FILES})

SET_TARGET_PROPERTIES(caersdk
    PROPERTIES
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

TARGET_LINK_LIBRARIES(caersdk ${CAER_LIBS})

IF (OS_WINDOWS)
    INSTALL(TARGETS caersdk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
ELSE()
    INSTALL(TARGETS caersdk DESTINATION ${CMAKE_INSTALL_LIBDIR})
ENDIF()

# Main cAER executable.
SET(CAER_SRC_FILES
	log.cpp
	config.cpp
	config_server.cpp
	module.cpp
	mainloop.cpp
	main.cpp)

# Set full RPATH
SET(CMAKE_INSTALL_RPATH ${CAER_LOCAL_PREFIX}/${CMAKE_INSTALL_BINDIR})

# Compile main caer executable.
ADD_EXECUTABLE(caer-bin ${CAER_SRC_FILES})
TARGET_LINK_LIBRARIES(caer-bin ${CAER_LIBS} caersdk)
INSTALL(TARGETS caer-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

# TCMalloc only makes sense for the main executable
IF (NOT USE_TCMALLOC)
	SET(USE_TCMALLOC 0 CACHE BOOL "Link to and use TCMalloc (Google Perftools) to provide faster memory allocation for caer-bin.")
ENDIF()

# TCMalloc support (fast memory allocation)
IF (USE_TCMALLOC)
	PKG_CHECK_MODULES(LIBTCMALLOC REQUIRED libtcmalloc>=2.2)

	TARGET_INCLUDE_DIRECTORIES(caer-bin PRIVATE ${LIBTCMALLOC_INCLUDE_DIRS})
	TARGET_LINK_LIBRARIES(caer-bin ${LIBTCMALLOC_LIBRARIES})
ENDIF()
