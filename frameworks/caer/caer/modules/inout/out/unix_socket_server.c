#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/mainloop.h"

#include "output_common.h"

static bool caerOutputUnixSocketServerInit(caerModuleData moduleData);

static const struct caer_module_functions OutputUnixSocketServerFunctions
	= {.moduleInit    = &caerOutputUnixSocketServerInit,
		.moduleRun    = &caerOutputCommonRun,
		.moduleConfig = NULL,
		.moduleExit   = &caerOutputCommonExit,
		.moduleReset  = &caerOutputCommonReset};

static const struct caer_event_stream_in OutputUnixSocketServerInputs[]
	= {{.type = -1, .number = -1, .readOnly = true}};

static const struct caer_module_info OutputUnixSockeServertInfo = {
	.version           = 1,
	.name              = "UnixSocketServerOutput",
	.description       = "Send AEDAT 3 data out through a Unix Socket to connected clients (server mode).",
	.type              = CAER_MODULE_OUTPUT,
	.memSize           = sizeof(struct output_common_state),
	.functions         = &OutputUnixSocketServerFunctions,
	.inputStreams      = OutputUnixSocketServerInputs,
	.inputStreamsSize  = CAER_EVENT_STREAM_IN_SIZE(OutputUnixSocketServerInputs),
	.outputStreams     = NULL,
	.outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&OutputUnixSockeServertInfo);
}

static bool caerOutputUnixSocketServerInit(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.
	sshsNodeCreateString(moduleData->moduleNode, "socketPath", "/tmp/caer.sock", 2, PATH_MAX, SSHS_FLAGS_NORMAL,
		"Unix Socket path for writing output data (server mode, create new socket).");
	sshsNodeCreateInt(
		moduleData->moduleNode, "backlogSize", 5, 1, 32, SSHS_FLAGS_NORMAL, "Maximum number of pending connections.");
	sshsNodeCreateInt(moduleData->moduleNode, "concurrentConnections", 10, 1, 128, SSHS_FLAGS_NORMAL,
		"Maximum number of concurrent active connections.");

	// Allocate memory.
	size_t numClients         = (size_t) sshsNodeGetInt(moduleData->moduleNode, "concurrentConnections");
	outputCommonNetIO streams = malloc(sizeof(*streams) + (numClients * sizeof(uv_stream_t *)));
	if (streams == NULL) {
		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for streams structure.");
		return (false);
	}

	streams->server = malloc(sizeof(uv_pipe_t));
	if (streams->server == NULL) {
		free(streams);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "Failed to allocate memory for network server.");
		return (false);
	}

	// Initialize common info.
	streams->isTCP         = false;
	streams->isUDP         = false;
	streams->isPipe        = true;
	streams->activeClients = 0;
	streams->clientsSize   = numClients;
	for (size_t i = 0; i < streams->clientsSize; i++) {
		streams->clients[i] = NULL;
	}

	// Remember address.
	streams->address = sshsNodeGetString(moduleData->moduleNode, "socketPath");

	streams->server->data = streams;

	// Initialize loop and network handles.
	int retVal = uv_loop_init(&streams->loop);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_loop_init", free(streams->server);
				 free(streams->address); free(streams); return (false));

	retVal = uv_pipe_init(&streams->loop, (uv_pipe_t *) streams->server, false);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_pipe_init", uv_loop_close(&streams->loop);
				 free(streams->server); free(streams->address); free(streams); return (false));

	retVal = uv_pipe_bind((uv_pipe_t *) streams->server, streams->address);
	UV_RET_CHECK(retVal, moduleData->moduleSubSystemString, "uv_pipe_bind", libuvCloseLoopHandles(&streams->loop);
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
