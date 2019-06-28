#include "caer-sdk/mainloop.h"

#include "output_common.h"

static bool caerOutputNetTCPServerInit(caerModuleData moduleData);

static const struct caer_module_functions OutputNetTCPServerFunctions = {.moduleInit = &caerOutputNetTCPServerInit,
	.moduleRun                                                                       = &caerOutputCommonRun,
	.moduleConfig                                                                    = NULL,
	.moduleExit                                                                      = &caerOutputCommonExit,
	.moduleReset                                                                     = &caerOutputCommonReset};

static const struct caer_event_stream_in OutputNetTCPServerInputs[] = {{.type = -1, .number = -1, .readOnly = true}};

static const struct caer_module_info OutputNetTCPServerInfo = {
	.version           = 1,
	.name              = "NetTCPServerOutput",
	.description       = "Send AEDAT 3 data out via TCP to connected clients (server mode).",
	.type              = CAER_MODULE_OUTPUT,
	.memSize           = sizeof(struct output_common_state),
	.functions         = &OutputNetTCPServerFunctions,
	.inputStreams      = OutputNetTCPServerInputs,
	.inputStreamsSize  = CAER_EVENT_STREAM_IN_SIZE(OutputNetTCPServerInputs),
	.outputStreams     = NULL,
	.outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&OutputNetTCPServerInfo);
}

static bool caerOutputNetTCPServerInit(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.
	sshsNodeCreateString(moduleData->moduleNode, "ipAddress", "127.0.0.1", 7, 15, SSHS_FLAGS_NORMAL,
		"IPv4 address to listen on (server mode).");
	sshsNodeCreateInt(moduleData->moduleNode, "portNumber", 7777, 1, UINT16_MAX, SSHS_FLAGS_NORMAL,
		"Port number to listen on (server mode).");
	sshsNodeCreateInt(
		moduleData->moduleNode, "backlogSize", 5, 1, 32, SSHS_FLAGS_NORMAL, "Maximum number of pending connections.");
	sshsNodeCreateInt(moduleData->moduleNode, "concurrentConnections", 10, 1, 128, SSHS_FLAGS_NORMAL,
		"Maximum number of concurrent active connections.");

	int retVal;

	// Generate address.
	struct sockaddr_in serverAddress;

	char *ipAddress = sshsNodeGetString(moduleData->moduleNode, "ipAddress");
	retVal          = uv_ip4_addr(ipAddress, sshsNodeGetInt(moduleData->moduleNode, "portNumber"), &serverAddress);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_ip4_addr", free(ipAddress); return (false));
	free(ipAddress);

	// Allocate memory.
	size_t numClients         = (size_t) sshsNodeGetInt(moduleData->moduleNode, "concurrentConnections");
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

	streams->server = malloc(sizeof(uv_tcp_t));
	if (streams->server == NULL) {
		free(streams->address);
		free(streams);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for network server.");
		return (false);
	}

	// Initialize common info.
	streams->isTCP         = true;
	streams->isUDP         = false;
	streams->isPipe        = false;
	streams->activeClients = 0;
	streams->clientsSize   = numClients;
	for (size_t i = 0; i < streams->clientsSize; i++) {
		streams->clients[i] = NULL;
	}

	// Remember address.
	memcpy(streams->address, &serverAddress, sizeof(struct sockaddr_in));

	streams->server->data = streams;

	// Initialize loop and network handles.
	retVal = uv_loop_init(&streams->loop);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_loop_init", free(streams->server);
				 free(streams->address); free(streams); return (false));

	retVal = uv_tcp_init(&streams->loop, (uv_tcp_t *) streams->server);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_tcp_init", uv_loop_close(&streams->loop);
				 free(streams->server); free(streams->address); free(streams); return (false));

	retVal = uv_tcp_bind((uv_tcp_t *) streams->server, streams->address, 0);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_tcp_bind", libuvCloseLoopHandles(&streams->loop);
				 uv_loop_close(&streams->loop); free(streams->address); free(streams); return (false));

	retVal = uv_listen(
		streams->server, sshsNodeGetInt(moduleData->moduleNode, "backlogSize"), &caerOutputCommonOnServerConnection);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_listen", libuvCloseLoopHandles(&streams->loop);
				 uv_loop_close(&streams->loop); free(streams->address); free(streams); return (false));

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
