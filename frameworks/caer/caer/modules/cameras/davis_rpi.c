#include "davis_utils.h"

static void caerInputDAVISRPiConfigInit(sshsNode moduleNode);
static bool caerInputDAVISRPiInit(caerModuleData moduleData);
static void caerInputDAVISRPiExit(caerModuleData moduleData);

static const struct caer_module_functions DAVISRPiFunctions = {.moduleConfigInit = &caerInputDAVISRPiConfigInit,
	.moduleInit                                                                  = &caerInputDAVISRPiInit,
	.moduleRun                                                                   = &caerInputDAVISCommonRun,
	.moduleConfig                                                                = NULL,
	.moduleExit                                                                  = &caerInputDAVISRPiExit,
	.moduleReset                                                                 = NULL};

static const struct caer_event_stream_out DAVISRPiOutputs[]
	= {{.type = SPECIAL_EVENT}, {.type = POLARITY_EVENT}, {.type = FRAME_EVENT}, {.type = IMU6_EVENT}};

static const struct caer_module_info DAVISRPiInfo = {
	.version           = 1,
	.name              = "DAVISRPi",
	.description       = "Connects to a DAVIS Raspberry-Pi camera module to get data.",
	.type              = CAER_MODULE_INPUT,
	.memSize           = 0,
	.functions         = &DAVISRPiFunctions,
	.inputStreams      = NULL,
	.inputStreamsSize  = 0,
	.outputStreams     = DAVISRPiOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DAVISRPiOutputs),
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&DAVISRPiInfo);
}

static void createDefaultAERConfiguration(caerModuleData moduleData, const char *nodePrefix);
static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo);

static void aerConfigSend(sshsNode node, caerModuleData moduleData);
static void aerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static void caerInputDAVISRPiConfigInit(sshsNode moduleNode) {
	caerInputDAVISCommonSystemConfigInit(moduleNode);
}

static bool caerInputDAVISRPiInit(caerModuleData moduleData) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	moduleData->moduleState = caerDeviceOpen(U16T(moduleData->moduleID), CAER_DEVICE_DAVIS_RPI, 0, 0, NULL);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	caerInputDAVISCommonInit(moduleData, &devInfo);

	// Create default settings and send them to the device.
	createDefaultBiasConfiguration(moduleData, chipIDToName(devInfo.chipID, true), devInfo.chipID);
	createDefaultLogicConfiguration(moduleData, chipIDToName(devInfo.chipID, true), &devInfo);
	createDefaultAERConfiguration(moduleData, chipIDToName(devInfo.chipID, true));
	sendDefaultConfiguration(moduleData, &devInfo);

	// Start data acquisition.
	bool ret = caerDeviceDataStart(moduleData->moduleState, &caerMainloopDataNotifyIncrease,
		&caerMainloopDataNotifyDecrease, NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

		return (false);
	}

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo.chipID, true));

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeAddAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeAddAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeAddAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");
	sshsNodeAddAttributeListener(apsNode, moduleData, &apsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeAddAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeAddAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode aerNode = sshsGetRelativeNode(deviceConfigNode, "aer/");
	sshsNodeAddAttributeListener(aerNode, moduleData, &aerConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodes    = sshsNodeGetChildren(biasNode, &biasNodesLength);

	if (biasNodes != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
		}

		free(biasNodes);
	}

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);
}

static void caerInputDAVISRPiExit(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);
	sshsNode deviceConfigNode      = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo.chipID, true));

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeRemoveAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeRemoveAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeRemoveAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");
	sshsNodeRemoveAttributeListener(apsNode, moduleData, &apsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeRemoveAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeRemoveAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode aerNode = sshsGetRelativeNode(deviceConfigNode, "aer/");
	sshsNodeRemoveAttributeListener(aerNode, moduleData, &aerConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodes    = sshsNodeGetChildren(biasNode, &biasNodesLength);

	if (biasNodes != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Remove listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
		}

		free(biasNodes);
	}

	// Ensure Exposure value is coherent with libcaer.
	sshsAttributeUpdaterRemoveAllForNode(apsNode);
	sshsNodePutAttribute(
		apsNode, "Exposure", SSHS_INT, apsExposureUpdater(moduleData->moduleState, "Exposure", SSHS_INT));

	// Remove statistics updaters.
	sshsNode statNode = sshsGetRelativeNode(deviceConfigNode, "statistics/");
	sshsAttributeUpdaterRemoveAllForNode(statNode);

	caerDeviceDataStop(moduleData->moduleState);

	caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);
}

static void createDefaultAERConfiguration(caerModuleData moduleData, const char *nodePrefix) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, nodePrefix);

	// Subsystem 9: DDR AER output configuration.
	sshsNode aerNode = sshsGetRelativeNode(deviceConfigNode, "aer/");
	sshsNodeCreateBool(aerNode, "Run", true, SSHS_FLAGS_NORMAL,
		"Enable the DDR AER output state machine (FPGA to Raspberry-Pi data exchange).");
}

static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(deviceConfigNode, "bias/"), moduleData, devInfo);
	chipConfigSend(sshsGetRelativeNode(deviceConfigNode, "chip/"), moduleData, devInfo);
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	aerConfigSend(sshsGetRelativeNode(deviceConfigNode, "aer/"), moduleData);
	muxConfigSend(sshsGetRelativeNode(deviceConfigNode, "multiplexer/"), moduleData);
	dvsConfigSend(sshsGetRelativeNode(deviceConfigNode, "dvs/"), moduleData, devInfo);
	apsConfigSend(sshsGetRelativeNode(deviceConfigNode, "aps/"), moduleData, devInfo);
	imuConfigSend(sshsGetRelativeNode(deviceConfigNode, "imu/"), moduleData, devInfo);
	extInputConfigSend(sshsGetRelativeNode(deviceConfigNode, "externalInput/"), moduleData, devInfo);
}

static void aerConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, sshsNodeGetBool(node, "Run"));
}

static void aerConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, changeValue.boolean);
		}
	}
}
