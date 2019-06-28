#include "caer-sdk/mainloop.h"
#include "output_common.h"

static bool caerOutputNetTCPInit(caerModuleData moduleData);

static const struct caer_module_functions OutputNetTCPFunctions = {.moduleInit = &caerOutputNetTCPInit,
	.moduleRun                                                                 = &caerOutputCommonRun,
	.moduleConfig                                                              = NULL,
	.moduleExit                                                                = &caerOutputCommonExit,
	.moduleReset                                                               = &caerOutputCommonReset};

static const struct caer_event_stream_in OutputNetTCPInputs[] = {{.type = -1, .number = -1, .readOnly = true}};

static const struct caer_module_info OutputNetTCPInfo = {
	.version           = 1,
	.name              = "NetTCPOutput",
	.description       = "Send AEDAT 3 data out via a TCP connection (client mode).",
	.type              = CAER_MODULE_OUTPUT,
	.memSize           = sizeof(struct output_common_state),
	.functions         = &OutputNetTCPFunctions,
	.inputStreams      = OutputNetTCPInputs,
	.inputStreamsSize  = CAER_EVENT_STREAM_IN_SIZE(OutputNetTCPInputs),
	.outputStreams     = NULL,
	.outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&OutputNetTCPInfo);
}

static bool caerOutputNetTCPInit(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.
	sshsNodeCreateString(moduleData->moduleNode, "ipAddress", "127.0.0.1", 7, 15, SSHS_FLAGS_NORMAL,
		"IPv4 address to connect to (client mode).");
	sshsNodeCreateInt(moduleData->moduleNode, "portNumber", 8888, 1, UINT16_MAX, SSHS_FLAGS_NORMAL,
		"Port number to connect to (client mode).");

	int retVal;

	// Generate address.
	struct sockaddr_in serverAddress;

	char *ipAddress = sshsNodeGetString(moduleData->moduleNode, "ipAddress");
	retVal          = uv_ip4_addr(ipAddress, sshsNodeGetInt(moduleData->moduleNode, "portNumber"), &serverAddress);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_ip4_addr", free(ipAddress); return (false));
	free(ipAddress);

	// Allocate memory.
	size_t numClients         = 1;
	outputCommonNetIO streams = malloc(sizeof(*streams) + (numClients * sizeof(uv_stream_t *)));
	if (streams == NULL) {
		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for streams structure.");
		return (false);
	}

	streams->address = malloc(sizeof(struct sockaddr_in));
	if (streams->address == NULL) {
		free(streams);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for network address.");
		return (false);
	}

	uv_tcp_t *tcp = malloc(sizeof(uv_tcp_t));
	if (tcp == NULL) {
		free(streams->address);
		free(streams);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for network structure.");
		return (false);
	}

	uv_connect_t *connectRequest = malloc(sizeof(uv_connect_t));
	if (connectRequest == NULL) {
		free(tcp);
		free(streams->address);
		free(streams);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for network connection.");
		return (false);
	}

	// Initialize common info.
	streams->isTCP         = true;
	streams->isUDP         = false;
	streams->isPipe        = false;
	streams->activeClients = 0;
	streams->clientsSize   = numClients;
	streams->clients[0]    = NULL;
	streams->server        = NULL;

	// Remember address.
	memcpy(streams->address, &serverAddress, sizeof(struct sockaddr_in));

	tcp->data = streams;

	// Initialize loop and network handles.
	retVal = uv_loop_init(&streams->loop);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_loop_init", free(connectRequest); free(tcp);
				 free(streams->address); free(streams); return (false));

	retVal = uv_tcp_init(&streams->loop, tcp);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_tcp_init", uv_loop_close(&streams->loop);
				 free(connectRequest); free(tcp); free(streams->address); free(streams); return (false));

	retVal = uv_tcp_nodelay(tcp, true);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_tcp_nodelay", libuvCloseLoopHandles(&streams->loop);
				 uv_loop_close(&streams->loop); free(connectRequest); free(streams->address); free(streams);
				 return (false));

	retVal = uv_tcp_connect(connectRequest, tcp, streams->address, &caerOutputCommonOnClientConnection);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_tcp_connect", libuvCloseLoopHandles(&streams->loop);
				 uv_loop_close(&streams->loop); free(connectRequest); free(streams->address); free(streams);
				 return (false));

	// Start.
	if (!caerOutputCommonInit(moduleData, -1, streams)) {
		libuvCloseLoopHandles(&streams->loop);
		uv_loop_close(&streams->loop);
		free(streams->address);
		free(streams);

		return (false);
	}

	return (true);
}
