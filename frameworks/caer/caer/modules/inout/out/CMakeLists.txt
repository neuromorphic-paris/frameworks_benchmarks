# TODO: Currently use libuv for output support.
PKG_CHECK_MODULES(LIBUV REQUIRED libuv>=1.7.5)

SET(OUTPUT_INCDIRS ${CAER_INCDIRS} ${LIBUV_INCLUDE_DIRS})
SET(OUTPUT_LIBDIRS ${CAER_LIBDIRS} ${LIBUV_LIBRARY_DIRS})
SET(OUTPUT_LIBS ${CAER_LIBS} ${LIBUV_LIBRARIES})

INCLUDE_DIRECTORIES(${OUTPUT_INCDIRS})
LINK_DIRECTORIES(${OUTPUT_LIBDIRS})

# FILE
ADD_LIBRARY(output_file SHARED output_common.c file.c)

SET_TARGET_PROPERTIES(output_file
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_file ${OUTPUT_LIBS})

INSTALL(TARGETS output_file DESTINATION ${CAER_MODULES_DIR})

# NET_TCP_SERVER
ADD_LIBRARY(output_net_tcp_server SHARED output_common.c net_tcp_server.c)

SET_TARGET_PROPERTIES(output_net_tcp_server
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_tcp_server ${OUTPUT_LIBS})

INSTALL(TARGETS output_net_tcp_server DESTINATION ${CAER_MODULES_DIR})

# NET_TCP_CLIENT
ADD_LIBRARY(output_net_tcp_client SHARED output_common.c net_tcp.c)

SET_TARGET_PROPERTIES(output_net_tcp_client
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_tcp_client ${OUTPUT_LIBS})

INSTALL(TARGETS output_net_tcp_client DESTINATION ${CAER_MODULES_DIR})

# NET_UDP
ADD_LIBRARY(output_net_udp SHARED output_common.c net_udp.c)

SET_TARGET_PROPERTIES(output_net_udp
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_udp ${OUTPUT_LIBS})

INSTALL(TARGETS output_net_udp DESTINATION ${CAER_MODULES_DIR})

# NET_SOCKET_SERVER
ADD_LIBRARY(output_net_socket_server SHARED output_common.c unix_socket_server.c)

SET_TARGET_PROPERTIES(output_net_socket_server
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_socket_server ${OUTPUT_LIBS})

INSTALL(TARGETS output_net_socket_server DESTINATION ${CAER_MODULES_DIR})

# NET_SOCKET_CLIENT
ADD_LIBRARY(output_net_socket_client SHARED output_common.c unix_socket.c)

SET_TARGET_PROPERTIES(output_net_socket_client
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(output_net_socket_client ${OUTPUT_LIBS})

INSTALL(TARGETS output_net_socket_client DESTINATION ${CAER_MODULES_DIR})
