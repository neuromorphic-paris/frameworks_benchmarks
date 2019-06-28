#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/mainloop.h"
#include "input_common.h"
#include <sys/socket.h>
#include <sys/un.h>

static bool caerInputUnixSocketInit(caerModuleData moduleData);

static const struct caer_module_functions InputUnixSocketFunctions = {.moduleInit = &caerInputUnixSocketInit,
	.moduleRun                                                                    = &caerInputCommonRun,
	.moduleConfig                                                                 = NULL,
	.moduleExit                                                                   = &caerInputCommonExit};

static const struct caer_event_stream_out InputUnixSocketOutputs[] = {{.type = -1}};

static const struct caer_module_info InputUnixSocketInfo = {
	.version           = 1,
	.name              = "UnixSocketInput",
	.description       = "Read AEDAT data from an existing Unix Socket.",
	.type              = CAER_MODULE_INPUT,
	.memSize           = sizeof(struct input_common_state),
	.functions         = &InputUnixSocketFunctions,
	.inputStreams      = NULL,
	.inputStreamsSize  = 0,
	.outputStreams     = InputUnixSocketOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(InputUnixSocketOutputs),
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&InputUnixSocketInfo);
}

static bool caerInputUnixSocketInit(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.
	sshsNodeCreateString(moduleData->moduleNode, "socketPath", "/tmp/caer.sock", 2, PATH_MAX, SSHS_FLAGS_NORMAL,
		"Unix Socket path for reading input data.");

	// Open an existing Unix local socket at a known path, where we'll write to.
	int sockFd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockFd < 0) {
		caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Could not create local Unix socket. Error: %d.", errno);
		return (false);
	}

	struct sockaddr_un unixSocketAddr;
	memset(&unixSocketAddr, 0, sizeof(struct sockaddr_un));

	unixSocketAddr.sun_family = AF_UNIX;

	char *socketPath = sshsNodeGetString(moduleData->moduleNode, "socketPath");
	strncpy(unixSocketAddr.sun_path, socketPath, sizeof(unixSocketAddr.sun_path) - 1);
	unixSocketAddr.sun_path[sizeof(unixSocketAddr.sun_path) - 1] = '\0'; // Ensure NUL terminated string.
	free(socketPath);

	// Connect socket to above address.
	if (connect(sockFd, (struct sockaddr *) &unixSocketAddr, sizeof(struct sockaddr_un)) < 0) {
		close(sockFd);

		caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Could not connect to local Unix socket. Error: %d.", errno);
		return (false);
	}

	if (!caerInputCommonInit(moduleData, sockFd, true, false)) {
		close(sockFd);
		return (false);
	}

	caerModuleLog(moduleData, CAER_LOG_INFO, "Local Unix socket ready at '%s'.", unixSocketAddr.sun_path);

	return (true);
}
