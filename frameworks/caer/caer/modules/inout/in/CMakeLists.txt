# FILE
ADD_LIBRARY(input_file SHARED input_common.c file.c)

SET_TARGET_PROPERTIES(input_file
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(input_file ${CAER_LIBS})

INSTALL(TARGETS input_file DESTINATION ${CAER_MODULES_DIR})

# NET_TCP_CLIENT
ADD_LIBRARY(input_net_tcp_client SHARED input_common.c net_tcp.c)

SET_TARGET_PROPERTIES(input_net_tcp_client
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(input_net_tcp_client ${CAER_LIBS})

INSTALL(TARGETS input_net_tcp_client DESTINATION ${CAER_MODULES_DIR})

# NET_SOCKET_CLIENT
ADD_LIBRARY(input_net_socket_client SHARED input_common.c unix_socket.c)

SET_TARGET_PROPERTIES(input_net_socket_client
	PROPERTIES
	PREFIX "caer_"
)

TARGET_LINK_LIBRARIES(input_net_socket_client ${CAER_LIBS})

INSTALL(TARGETS input_net_socket_client DESTINATION ${CAER_MODULES_DIR})
