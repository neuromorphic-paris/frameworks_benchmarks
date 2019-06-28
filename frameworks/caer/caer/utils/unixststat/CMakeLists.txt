# No Unix sockets on Windows.
IF (NOT OS_WINDOWS)
	# Compile Unix local stream statistics program
	ADD_EXECUTABLE(unixststat unixststat.c)
	TARGET_LINK_LIBRARIES(unixststat ${LIBCAER_LIBRARIES})
	INSTALL(TARGETS unixststat DESTINATION ${CMAKE_INSTALL_BINDIR})
ENDIF()
