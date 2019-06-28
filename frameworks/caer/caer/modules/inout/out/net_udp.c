#include "caer-sdk/mainloop.h"
#include "output_common.h"

static bool caerOutputNetUDPInit(caerModuleData moduleData);

static const struct caer_module_functions OutputNetUDPFunctions = {.moduleInit = &caerOutputNetUDPInit,
	.moduleRun                                                                 = &caerOutputCommonRun,
	.moduleConfig                                                              = NULL,
	.moduleExit                                                                = &caerOutputCommonExit,
	.moduleReset                                                               = &caerOutputCommonReset};

static const struct caer_event_stream_in OutputNetUDPInputs[] = {{.type = -1, .number = -1, .readOnly = true}};

static const struct caer_module_info OutputNetUDPInfo = {
	.version           = 1,
	.name              = "NetUDPOutput",
	.description       = "Send AEDAT 3 data out via UDP messages.",
	.type              = CAER_MODULE_OUTPUT,
	.memSize           = sizeof(struct output_common_state),
	.functions         = &OutputNetUDPFunctions,
	.inputStreams      = OutputNetUDPInputs,
	.inputStreamsSize  = CAER_EVENT_STREAM_IN_SIZE(OutputNetUDPInputs),
	.outputStreams     = NULL,
	.outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&OutputNetUDPInfo);
}

static bool caerOutputNetUDPInit(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.
	sshsNodeCreateString(moduleData->moduleNode, "ipAddress", "127.0.0.1", 7, 15, SSHS_FLAGS_NORMAL,
		"IPv4 address to connect to (client mode).");
	sshsNodeCreateInt(moduleData->moduleNode, "portNumber", 6666, 1, UINT16_MAX, SSHS_FLAGS_NORMAL,
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

	uv_udp_t *udp = malloc(sizeof(uv_udp_t));
	if (udp == NULL) {
		free(streams->address);
		free(streams);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for network structure.");
		return (false);
	}

	// Initialize common info.
	streams->isTCP         = false;
	streams->isUDP         = true;
	streams->isPipe        = false;
	streams->activeClients = 0;
	streams->clientsSize   = numClients;
	streams->clients[0]    = NULL;
	streams->server        = NULL;

	// Remember address.
	memcpy(streams->address, &serverAddress, sizeof(struct sockaddr_in));

	udp->data = streams;

	// Assign here instead of caerOutputCommonOnClientConnection(), since that doesn't
	// exist for UDP connections in libuv.
	streams->clients[0]    = (uv_stream_t *) udp;
	streams->activeClients = 1;

	// Initialize loop and network handles.
	retVal = uv_loop_init(&streams->loop);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_loop_init", free(udp); free(streams->address);
				 free(streams); return (false));

	retVal = uv_udp_init(&streams->loop, udp);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_udp_init", uv_loop_close(&streams->loop); free(udp);
				 free(streams->address); free(streams); return (false));

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
