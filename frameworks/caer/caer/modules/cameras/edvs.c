#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/special.h>

#include <libcaer/devices/edvs.h>

#include "caer-sdk/mainloop.h"

static void caerInputEDVSConfigInit(sshsNode moduleNode);
static bool caerInputEDVSInit(caerModuleData moduleData);
static void caerInputEDVSRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
// CONFIG: Nothing to do here in the main thread!
// All configuration is asynchronous through SSHS listeners.
static void caerInputEDVSExit(caerModuleData moduleData);

static const struct caer_module_functions EDVSFunctions = {.moduleConfigInit = &caerInputEDVSConfigInit,
	.moduleInit                                                              = &caerInputEDVSInit,
	.moduleRun                                                               = &caerInputEDVSRun,
	.moduleConfig                                                            = NULL,
	.moduleExit                                                              = &caerInputEDVSExit,
	.moduleReset                                                             = NULL};

static const struct caer_event_stream_out EDVSOutputs[] = {{.type = SPECIAL_EVENT}, {.type = POLARITY_EVENT}};

static const struct caer_module_info EDVSInfo = {
	.version           = 1,
	.name              = "eDVS",
	.description       = "Connects to an eDVS/minieDVS camera to get data.",
	.type              = CAER_MODULE_INPUT,
	.memSize           = 0,
	.functions         = &EDVSFunctions,
	.inputStreams      = NULL,
	.inputStreamsSize  = 0,
	.outputStreams     = EDVSOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(EDVSOutputs),
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&EDVSInfo);
}

static void sendDefaultConfiguration(caerModuleData moduleData);
static void moduleShutdownNotify(void *p);
static void biasConfigSend(sshsNode node, caerModuleData moduleData);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void dvsConfigSend(sshsNode node, caerModuleData moduleData);
static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void serialConfigSend(sshsNode node, caerModuleData moduleData);
static void serialConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigSend(sshsNode node, caerModuleData moduleData);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static void caerInputEDVSConfigInit(sshsNode moduleNode) {
	// Serial port settings.
	sshsNodeCreateString(
		moduleNode, "serialPort", "/dev/ttyUSB0", 0, 128, SSHS_FLAGS_NORMAL, "Serial port to connect to.");
	sshsNodeCreateInt(moduleNode, "baudRate", CAER_HOST_CONFIG_SERIAL_BAUD_RATE_12M, 0, 20000000, SSHS_FLAGS_NORMAL,
		"Baud-rate for serial port.");

	// Add auto-restart setting.
	sshsNodeCreateBool(
		moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL, "Automatically restart module after shutdown.");

	// Set default biases, from EDVSFast.xml settings.
	sshsNode biasNode = sshsGetRelativeNode(moduleNode, "bias/");
	sshsNodeCreateInt(biasNode, "cas", 1992, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "Photoreceptor cascode.");
	sshsNodeCreateInt(
		biasNode, "injGnd", 1108364, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "Differentiator switch level.");
	sshsNodeCreateInt(biasNode, "reqPd", 16777215, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "AER request pull-down.");
	sshsNodeCreateInt(
		biasNode, "puX", 8159221, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "2nd dimension AER static pull-up.");
	sshsNodeCreateInt(
		biasNode, "diffOff", 132, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "OFF threshold - lower to raise threshold.");
	sshsNodeCreateInt(biasNode, "req", 309590, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "OFF request inverter bias.");
	sshsNodeCreateInt(biasNode, "refr", 969, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "Refractory period.");
	sshsNodeCreateInt(
		biasNode, "puY", 16777215, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "1st dimension AER static pull-up.");
	sshsNodeCreateInt(biasNode, "diffOn", 209996, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL,
		"ON threshold - higher to raise threshold.");
	sshsNodeCreateInt(biasNode, "diff", 13125, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "Differentiator.");
	sshsNodeCreateInt(biasNode, "foll", 271, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL,
		"Source follower buffer between photoreceptor and differentiator.");
	sshsNodeCreateInt(biasNode, "pr", 217, 0, (0x01 << 24) - 1, SSHS_FLAGS_NORMAL, "Photoreceptor.");

	// DVS settings.
	sshsNode dvsNode = sshsGetRelativeNode(moduleNode, "dvs/");
	sshsNodeCreateBool(dvsNode, "Run", true, SSHS_FLAGS_NORMAL, "Run DVS to get polarity events.");
	sshsNodeCreateBool(dvsNode, "TimestampReset", false, SSHS_FLAGS_NOTIFY_ONLY, "Reset timestamps to zero.");

	// Serial communication buffer settings.
	sshsNode serialNode = sshsGetRelativeNode(moduleNode, "serial/");
	sshsNodeCreateInt(serialNode, "ReadSize", 1024, 128, 32768, SSHS_FLAGS_NORMAL,
		"Size in bytes of data buffer for serial port read operations.");

	sshsNode sysNode = sshsGetRelativeNode(moduleNode, "system/");

	// Packet settings (size (in events) and time interval (in µs)).
	sshsNodeCreateInt(sysNode, "PacketContainerMaxPacketSize", 0, 0, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Maximum packet size in events, when any packet reaches this size, the EventPacketContainer is sent for "
		"processing.");
	sshsNodeCreateInt(sysNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL,
		"Time interval in µs, each sent EventPacketContainer will span this interval.");

	// Ring-buffer setting (only changes value on module init/shutdown cycles).
	sshsNodeCreateInt(sysNode, "DataExchangeBufferSize", 64, 8, 1024, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer queue, used for transfers between data acquisition thread and mainloop.");
}

static bool caerInputEDVSInit(caerModuleData moduleData) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	char *serialPortName    = sshsNodeGetString(moduleData->moduleNode, "serialPort");
	moduleData->moduleState = caerDeviceOpenSerial(U16T(moduleData->moduleID), CAER_DEVICE_EDVS, serialPortName,
		U32T(sshsNodeGetInt(moduleData->moduleNode, "baudRate")));
	free(serialPortName);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Put global source information into SSHS.
	struct caer_edvs_info devInfo = caerEDVSInfoGet(moduleData->moduleState);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Timestamp synchronization support: device master status.");

	sshsNodeCreateInt(sourceInfoNode, "polaritySizeX", devInfo.dvsSizeX, devInfo.dvsSizeX, devInfo.dvsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events width.");
	sshsNodeCreateInt(sourceInfoNode, "polaritySizeY", devInfo.dvsSizeY, devInfo.dvsSizeY, devInfo.dvsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events height.");

	// Put source information for generic visualization, to be used to display and debug filter information.
	sshsNodeCreateInt(sourceInfoNode, "dataSizeX", devInfo.dvsSizeX, devInfo.dvsSizeX, devInfo.dvsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateInt(sourceInfoNode, "dataSizeY", devInfo.dvsSizeY, devInfo.dvsSizeY, devInfo.dvsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": eDVS4337\r\n", moduleData->moduleID);

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": eDVS4337\r\n", moduleData->moduleID);
	sourceString[sourceStringLength] = '\0';

	sshsNodeCreateString(sourceInfoNode, "sourceString", sourceString, sourceStringLength, sourceStringLength,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device source information.");

	// Ensure good defaults for data acquisition settings.
	// No blocking behavior due to mainloop notification, and no auto-start of
	// all producers to ensure cAER settings are respected.
	caerDeviceConfigSet(
		moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, false);
	caerDeviceConfigSet(
		moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_START_PRODUCERS, false);
	caerDeviceConfigSet(
		moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_STOP_PRODUCERS, true);

	// Create default settings and send them to the device.
	sendDefaultConfiguration(moduleData);

	// Start data acquisition.
	bool ret = caerDeviceDataStart(moduleData->moduleState, &caerMainloopDataNotifyIncrease,
		&caerMainloopDataNotifyDecrease, NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");
	sshsNodeAddAttributeListener(biasNode, moduleData, &biasConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(moduleData->moduleNode, "dvs/");
	sshsNodeAddAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode serialNode = sshsGetRelativeNode(moduleData->moduleNode, "serial/");
	sshsNodeAddAttributeListener(serialNode, moduleData, &serialConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);
}

static void caerInputEDVSExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode biasNode = sshsGetRelativeNode(moduleData->moduleNode, "bias/");
	sshsNodeRemoveAttributeListener(biasNode, moduleData, &biasConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(moduleData->moduleNode, "dvs/");
	sshsNodeRemoveAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode serialNode = sshsGetRelativeNode(moduleData->moduleNode, "serial/");
	sshsNodeRemoveAttributeListener(serialNode, moduleData, &serialConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

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

static void caerInputEDVSRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	*out = caerDeviceDataGet(moduleData->moduleState);

	if (*out != NULL) {
		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindValidEventByTypeConst((caerSpecialEventPacketConst) special, TIMESTAMP_RESET)
				   != NULL)) {
			caerMainloopModuleResetOutputRevDeps(moduleData->moduleID);
		}
	}
}

static void sendDefaultConfiguration(caerModuleData moduleData) {
	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "bias/"), moduleData);
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	serialConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "serial/"), moduleData);
	dvsConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "dvs/"), moduleData);
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void biasConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_CAS, U32T(sshsNodeGetInt(node, "cas")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_INJGND, U32T(sshsNodeGetInt(node, "injGnd")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_REQPD, U32T(sshsNodeGetInt(node, "reqPd")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PUX, U32T(sshsNodeGetInt(node, "puX")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_DIFFOFF, U32T(sshsNodeGetInt(node, "diffOff")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_REQ, U32T(sshsNodeGetInt(node, "req")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_REFR, U32T(sshsNodeGetInt(node, "refr")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PUY, U32T(sshsNodeGetInt(node, "puY")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_DIFFON, U32T(sshsNodeGetInt(node, "diffOn")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_DIFF, U32T(sshsNodeGetInt(node, "diff")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_FOLL, U32T(sshsNodeGetInt(node, "foll")));
	caerDeviceConfigSet(
		moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PR, U32T(sshsNodeGetInt(node, "pr")));
}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "cas")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_CAS, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "injGnd")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_INJGND, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "reqPd")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_REQPD, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "puX")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PUX, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "diffOff")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_DIFFOFF, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "req")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_REQ, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "refr")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_REFR, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "puY")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PUY, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "diffOn")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_DIFFON, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "diff")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_DIFF, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "foll")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_FOLL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "pr")) {
			caerDeviceConfigSet(moduleData->moduleState, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PR, U32T(changeValue.iint));
		}
	}
}

static void dvsConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, EDVS_CONFIG_DVS, EDVS_CONFIG_DVS_TIMESTAMP_RESET,
		sshsNodeGetBool(node, "TimestampReset"));
	caerDeviceConfigSet(moduleData->moduleState, EDVS_CONFIG_DVS, EDVS_CONFIG_DVS_RUN, sshsNodeGetBool(node, "Run"));
}

static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampReset")) {
			caerDeviceConfigSet(
				moduleData->moduleState, EDVS_CONFIG_DVS, EDVS_CONFIG_DVS_TIMESTAMP_RESET, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, EDVS_CONFIG_DVS, EDVS_CONFIG_DVS_RUN, changeValue.boolean);
		}
	}
}

static void serialConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_SERIAL, CAER_HOST_CONFIG_SERIAL_READ_SIZE,
		U32T(sshsNodeGetInt(node, "ReadSize")));
}

static void serialConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "ReadSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_SERIAL, CAER_HOST_CONFIG_SERIAL_READ_SIZE,
				U32T(changeValue.iint));
		}
	}
}

static void systemConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
		CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(sshsNodeGetInt(node, "PacketContainerMaxPacketSize")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
		CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(sshsNodeGetInt(node, "PacketContainerInterval")));

	// Changes only take effect on module start!
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
		CAER_HOST_CONFIG_DATAEXCHANGE_BUFFER_SIZE, U32T(sshsNodeGetInt(node, "DataExchangeBufferSize")));
}

static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerMaxPacketSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
				CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
				CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(changeValue.iint));
		}
	}
}

static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_INT && caerStrEquals(changeKey, "logLevel")) {
		caerDeviceConfigSet(
			moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL, U32T(changeValue.iint));
	}
}
