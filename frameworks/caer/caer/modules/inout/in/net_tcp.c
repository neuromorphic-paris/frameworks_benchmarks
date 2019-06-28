#include "caer-sdk/mainloop.h"
#include "input_common.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

static bool caerInputNetTCPInit(caerModuleData moduleData);

static const struct caer_module_functions InputNetTCPFunctions = {.moduleInit = &caerInputNetTCPInit,
	.moduleRun                                                                = &caerInputCommonRun,
	.moduleConfig                                                             = NULL,
	.moduleExit                                                               = &caerInputCommonExit};

static const struct caer_event_stream_out InputNetTCPOutputs[] = {{.type = -1}};

static const struct caer_module_info InputNetTCPInfo = {
	.version           = 1,
	.name              = "NetTCPInput",
	.description       = "Read AEDAT data from a TCP server.",
	.type              = CAER_MODULE_INPUT,
	.memSize           = sizeof(struct input_common_state),
	.functions         = &InputNetTCPFunctions,
	.inputStreams      = NULL,
	.inputStreamsSize  = 0,
	.outputStreams     = InputNetTCPOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(InputNetTCPOutputs),
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&InputNetTCPInfo);
}

static bool caerInputNetTCPInit(caerModuleData moduleData) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.
	sshsNodeCreateString(
		moduleData->moduleNode, "ipAddress", "127.0.0.1", 7, 15, SSHS_FLAGS_NORMAL, "IPv4 address to connect to.");
	sshsNodeCreateInt(
		moduleData->moduleNode, "portNumber", 7777, 1, UINT16_MAX, SSHS_FLAGS_NORMAL, "Port number to connect to.");

	// Open a TCP socket to the remote client, to which we'll send data packets.
	int sockFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockFd < 0) {
		caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Could not create TCP socket. Error: %d.", errno);
		return (false);
	}

	struct sockaddr_in tcpClient;
	memset(&tcpClient, 0, sizeof(struct sockaddr_in));

	tcpClient.sin_family = AF_INET;
	tcpClient.sin_port   = htons(U16T(sshsNodeGetInt(moduleData->moduleNode, "portNumber")));

	char *ipAddress = sshsNodeGetString(moduleData->moduleNode, "ipAddress");
	if (inet_pton(AF_INET, ipAddress, &tcpClient.sin_addr) == 0) {
		close(sockFd);

		caerModuleLog(moduleData, CAER_LOG_CRITICAL, "No valid IP address found. '%s' is invalid!", ipAddress);

		free(ipAddress);
		return (false);
	}
	free(ipAddress);

	if (connect(sockFd, (struct sockaddr *) &tcpClient, sizeof(struct sockaddr_in)) != 0) {
		close(sockFd);

		caerModuleLog(moduleData, CAER_LOG_CRITICAL,
			"Could not connect to remote TCP server %s:%" PRIu16 ". Error: %d.",
			inet_ntop(AF_INET, &tcpClient.sin_addr, (char[INET_ADDRSTRLEN]){0x00}, INET_ADDRSTRLEN),
			ntohs(tcpClient.sin_port), errno);
		return (false);
	}

	if (!caerInputCommonInit(moduleData, sockFd, true, false)) {
		close(sockFd);
		return (false);
	}

	caerModuleLog(moduleData, CAER_LOG_INFO, "TCP socket connected to %s:%" PRIu16 ".",
		inet_ntop(AF_INET, &tcpClient.sin_addr, (char[INET_ADDRSTRLEN]){0x00}, INET_ADDRSTRLEN),
		ntohs(tcpClient.sin_port));

	return (true);
}
