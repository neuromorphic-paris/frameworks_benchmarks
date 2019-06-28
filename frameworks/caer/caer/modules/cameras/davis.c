#include <libcaer/events/sample.h>

#include "davis_utils.h"

static void caerInputDAVISConfigInit(sshsNode moduleNode);
static bool caerInputDAVISInit(caerModuleData moduleData);
static void caerInputDAVISExit(caerModuleData moduleData);

static const struct caer_module_functions DAVISFunctions = {.moduleConfigInit = &caerInputDAVISConfigInit,
	.moduleInit                                                               = &caerInputDAVISInit,
	.moduleRun                                                                = &caerInputDAVISCommonRun,
	.moduleConfig                                                             = NULL,
	.moduleExit                                                               = &caerInputDAVISExit,
	.moduleReset                                                              = NULL};

static const struct caer_event_stream_out DAVISOutputs[]
	= {{.type = SPECIAL_EVENT}, {.type = POLARITY_EVENT}, {.type = FRAME_EVENT}, {.type = IMU6_EVENT}};

static const struct caer_module_info DAVISInfo = {
	.version           = 1,
	.name              = "DAVIS",
	.description       = "Connects to a DAVIS camera to get data.",
	.type              = CAER_MODULE_INPUT,
	.memSize           = 0,
	.functions         = &DAVISFunctions,
	.inputStreams      = NULL,
	.inputStreamsSize  = 0,
	.outputStreams     = DAVISOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DAVISOutputs),
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&DAVISInfo);
}

static void createDefaultUSBConfiguration(caerModuleData moduleData, const char *nodePrefix);
static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo);

static void usbConfigSend(sshsNode node, caerModuleData moduleData);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static void caerInputDAVISConfigInit(sshsNode moduleNode) {
	// USB port/bus/SN settings/restrictions.
	// These can be used to force connection to one specific device at startup.
	sshsNodeCreateInt(moduleNode, "busNumber", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL, "USB bus number restriction.");
	sshsNodeCreateInt(moduleNode, "devAddress", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL, "USB device address restriction.");
	sshsNodeCreateString(moduleNode, "serialNumber", "", 0, 8, SSHS_FLAGS_NORMAL, "USB serial number restriction.");

	// Add auto-restart setting.
	sshsNodeCreateBool(
		moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL, "Automatically restart module after shutdown.");

	caerInputDAVISCommonSystemConfigInit(moduleNode);
}

static bool caerInputDAVISInit(caerModuleData moduleData) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	char *serialNumber      = sshsNodeGetString(moduleData->moduleNode, "serialNumber");
	moduleData->moduleState = caerDeviceOpen(U16T(moduleData->moduleID), CAER_DEVICE_DAVIS,
		U8T(sshsNodeGetInt(moduleData->moduleNode, "busNumber")),
		U8T(sshsNodeGetInt(moduleData->moduleNode, "devAddress")), serialNumber);
	free(serialNumber);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	caerInputDAVISCommonInit(moduleData, &devInfo);

	// Generate sub-system string for module.
	char *prevAdditionStart = strchr(moduleData->moduleSubSystemString, '[');

	if (prevAdditionStart != NULL) {
		*prevAdditionStart = '\0';
	}

	size_t subSystemStringLength
		= (size_t) snprintf(NULL, 0, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]", moduleData->moduleSubSystemString,
			devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber, devInfo.deviceUSBDeviceAddress);

	char subSystemString[subSystemStringLength + 1];
	snprintf(subSystemString, subSystemStringLength + 1, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber,
		devInfo.deviceUSBDeviceAddress);
	subSystemString[subSystemStringLength] = '\0';

	caerModuleSetSubSystemString(moduleData, subSystemString);

	// Create default settings and send them to the device.
	createDefaultBiasConfiguration(moduleData, chipIDToName(devInfo.chipID, true), devInfo.chipID);
	createDefaultLogicConfiguration(moduleData, chipIDToName(devInfo.chipID, true), &devInfo);
	createDefaultUSBConfiguration(moduleData, chipIDToName(devInfo.chipID, true));
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

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

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

static void caerInputDAVISExit(caerModuleData moduleData) {
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

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeRemoveAttributeListener(usbNode, moduleData, &usbConfigListener);

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

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

static void createDefaultUSBConfiguration(caerModuleData moduleData, const char *nodePrefix) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, nodePrefix);

	// Subsystem 9: FX2/3 USB Configuration and USB buffer settings.
	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeCreateBool(
		usbNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable the USB state machine (FPGA to USB data exchange).");
	sshsNodeCreateInt(usbNode, "EarlyPacketDelay", 8, 1, 8000, SSHS_FLAGS_NORMAL,
		"Send early USB packets if this timeout is reached (in 125µs time-slices).");

	sshsNodeCreateInt(usbNode, "BufferNumber", 8, 2, 128, SSHS_FLAGS_NORMAL, "Number of USB transfers.");
	sshsNodeCreateInt(
		usbNode, "BufferSize", 8192, 512, 32768, SSHS_FLAGS_NORMAL, "Size in bytes of data buffers for USB transfers.");
}

static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(deviceConfigNode, "bias/"), moduleData, devInfo);
	chipConfigSend(sshsGetRelativeNode(deviceConfigNode, "chip/"), moduleData, devInfo);
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	usbConfigSend(sshsGetRelativeNode(deviceConfigNode, "usb/"), moduleData);
	muxConfigSend(sshsGetRelativeNode(deviceConfigNode, "multiplexer/"), moduleData);
	dvsConfigSend(sshsGetRelativeNode(deviceConfigNode, "dvs/"), moduleData, devInfo);
	apsConfigSend(sshsGetRelativeNode(deviceConfigNode, "aps/"), moduleData, devInfo);
	imuConfigSend(sshsGetRelativeNode(deviceConfigNode, "imu/"), moduleData, devInfo);
	extInputConfigSend(sshsGetRelativeNode(deviceConfigNode, "externalInput/"), moduleData, devInfo);
}

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
		U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
		U32T(sshsNodeGetInt(node, "BufferSize")));

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_EARLY_PACKET_DELAY,
		U32T(sshsNodeGetInt(node, "EarlyPacketDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_RUN, sshsNodeGetBool(node, "Run"));
}

static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferNumber")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "EarlyPacketDelay")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_EARLY_PACKET_DELAY, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_RUN, changeValue.boolean);
		}
	}
}
