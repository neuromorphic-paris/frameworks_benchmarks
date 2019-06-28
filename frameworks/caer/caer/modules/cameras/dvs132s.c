#include <libcaer/events/imu6.h>
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/special.h>

#include <libcaer/devices/dvs132s.h>

#include <caer-sdk/cross/c11threads_posix.h>
#include <caer-sdk/mainloop.h>

static void caerInputDVS132SConfigInit(sshsNode moduleNode);
static bool caerInputDVS132SInit(caerModuleData moduleData);
static void caerInputDVS132SRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerInputDVS132SExit(caerModuleData moduleData);

static const struct caer_module_functions DVS132SFunctions = {
	.moduleConfigInit = &caerInputDVS132SConfigInit,
	.moduleInit       = &caerInputDVS132SInit,
	.moduleRun        = &caerInputDVS132SRun,
	.moduleConfig     = NULL,
	.moduleExit       = &caerInputDVS132SExit,
	.moduleReset      = NULL,
};

static const struct caer_event_stream_out DVS132SOutputs[] = {
	{.type = SPECIAL_EVENT},
	{.type = POLARITY_EVENT},
	{.type = IMU6_EVENT},
};

static const struct caer_module_info DVS132SInfo = {
	.version           = 1,
	.name              = "DVS132S",
	.description       = "Connects to a DVS132S camera to get data.",
	.type              = CAER_MODULE_INPUT,
	.memSize           = 0,
	.functions         = &DVS132SFunctions,
	.inputStreams      = NULL,
	.inputStreamsSize  = 0,
	.outputStreams     = DVS132SOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(DVS132SOutputs),
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&DVS132SInfo);
}

static void moduleShutdownNotify(void *p);

static void createDefaultBiasConfiguration(caerModuleData moduleData);
static void createDefaultLogicConfiguration(caerModuleData moduleData, const struct caer_dvs132s_info *devInfo);
static void createDefaultUSBConfiguration(caerModuleData moduleData);
static void sendDefaultConfiguration(caerModuleData moduleData, const struct caer_dvs132s_info *devInfo);

static void biasConfigSend(sshsNode node, caerModuleData moduleData);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void muxConfigSend(sshsNode node, caerModuleData moduleData);
static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void dvsConfigSend(sshsNode node, caerModuleData moduleData);
static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void imuConfigSend(sshsNode node, caerModuleData moduleData);
static void imuConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void extInputConfigSend(sshsNode node, caerModuleData moduleData, const struct caer_dvs132s_info *devInfo);
static void extInputConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigSend(sshsNode node, caerModuleData moduleData);
static void usbConfigSend(sshsNode node, caerModuleData moduleData);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static union sshs_node_attr_value statisticsUpdater(
	void *userData, const char *key, enum sshs_node_attr_value_type type);

static void caerInputDVS132SConfigInit(sshsNode moduleNode) {
	// USB port/bus/SN settings/restrictions.
	// These can be used to force connection to one specific device at startup.
	sshsNodeCreateInt(moduleNode, "busNumber", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL, "USB bus number restriction.");
	sshsNodeCreateInt(moduleNode, "devAddress", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL, "USB device address restriction.");
	sshsNodeCreateString(moduleNode, "serialNumber", "", 0, 8, SSHS_FLAGS_NORMAL, "USB serial number restriction.");

	// Add auto-restart setting.
	sshsNodeCreateBool(
		moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL, "Automatically restart module after shutdown.");

	sshsNode sysNode = sshsGetRelativeNode(moduleNode, "system/");

	// Packet settings (size (in events) and time interval (in µs)).
	sshsNodeCreateInt(sysNode, "PacketContainerMaxPacketSize", 0, 0, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Maximum packet size in events, when any packet reaches "
		"this size, the EventPacketContainer is sent for "
		"processing.");
	sshsNodeCreateInt(sysNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL,
		"Time interval in µs, each sent EventPacketContainer will "
		"span this interval.");

	// Ring-buffer setting (only changes value on module init/shutdown cycles).
	sshsNodeCreateInt(sysNode, "DataExchangeBufferSize", 64, 8, 1024, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer queue, used for transfers "
		"between data acquisition thread and mainloop.");
}

static bool caerInputDVS132SInit(caerModuleData moduleData) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// Start data acquisition, and correctly notify mainloop of new data and
	// module of exceptional shutdown cases (device pulled, ...).
	char *serialNumber      = sshsNodeGetString(moduleData->moduleNode, "serialNumber");
	moduleData->moduleState = caerDeviceOpen(U16T(moduleData->moduleID), CAER_DEVICE_DVS132S,
		U8T(sshsNodeGetInt(moduleData->moduleNode, "busNumber")),
		U8T(sshsNodeGetInt(moduleData->moduleNode, "devAddress")), serialNumber);
	free(serialNumber);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Put global source information into SSHS.
	struct caer_dvs132s_info devInfo = caerDVS132SInfoGet(moduleData->moduleState);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateInt(sourceInfoNode, "firmwareVersion", devInfo.firmwareVersion, devInfo.firmwareVersion,
		devInfo.firmwareVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device USB firmware version.");
	sshsNodeCreateInt(sourceInfoNode, "logicVersion", devInfo.logicVersion, devInfo.logicVersion, devInfo.logicVersion,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device FPGA logic version.");
	sshsNodeCreateInt(sourceInfoNode, "chipID", devInfo.chipID, devInfo.chipID, devInfo.chipID,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device chip identification number.");

	sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Timestamp synchronization support: device master status.");
	sshsNodeCreateInt(sourceInfoNode, "polaritySizeX", devInfo.dvsSizeX, devInfo.dvsSizeX, devInfo.dvsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events width.");
	sshsNodeCreateInt(sourceInfoNode, "polaritySizeY", devInfo.dvsSizeY, devInfo.dvsSizeY, devInfo.dvsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events height.");

	// Extra features.
	sshsNodeCreateBool(sourceInfoNode, "muxHasStatistics", devInfo.muxHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA Multiplexer statistics (USB event drops).");
	sshsNodeCreateBool(sourceInfoNode, "extInputHasGenerator", devInfo.extInputHasGenerator,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports generating pulses on output signal connector.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasStatistics", devInfo.dvsHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS statistics.");

	// Put source information for generic visualization, to be used to display and
	// debug filter information.
	int16_t dataSizeX = devInfo.dvsSizeX;
	int16_t dataSizeY = devInfo.dvsSizeY;

	sshsNodeCreateInt(sourceInfoNode, "dataSizeX", dataSizeX, dataSizeX, dataSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateInt(sourceInfoNode, "dataSizeY", dataSizeY, dataSizeY, dataSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");

	// Generate source string for output modules.
	size_t sourceStringLength
		= (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID, "DVS132S");

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID, "DVS132S");
	sourceString[sourceStringLength] = '\0';

	sshsNodeCreateString(sourceInfoNode, "sourceString", sourceString, sourceStringLength, sourceStringLength,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device source information.");

	// Generate sub-system string for module.
	size_t subSystemStringLength
		= (size_t) snprintf(NULL, 0, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]", moduleData->moduleSubSystemString,
			devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber, devInfo.deviceUSBDeviceAddress);

	char subSystemString[subSystemStringLength + 1];
	snprintf(subSystemString, subSystemStringLength + 1, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber,
		devInfo.deviceUSBDeviceAddress);
	subSystemString[subSystemStringLength] = '\0';

	caerModuleSetSubSystemString(moduleData, subSystemString);

	// Ensure good defaults for data acquisition settings.
	// No blocking behavior due to mainloop notification, and no auto-start of
	// all producers to ensure cAER settings are respected.
	caerDeviceConfigSet(
		moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, false);
	caerDeviceConfigSet(
		moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_START_PRODUCERS, false);
	caerDeviceConfigSet(
		moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_STOP_PRODUCERS, true);

	// Create default settings.
	createDefaultBiasConfiguration(moduleData);
	createDefaultLogicConfiguration(moduleData, &devInfo);
	createDefaultUSBConfiguration(moduleData);

	// Start data acquisition.
	bool ret = caerDeviceDataStart(moduleData->moduleState, &caerMainloopDataNotifyIncrease,
		&caerMainloopDataNotifyDecrease, NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

		return (false);
	}

	// Send configuration, enabling data capture as requested.
	sendDefaultConfiguration(moduleData, &devInfo);

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, "DVS132S/");

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");
	sshsNodeAddAttributeListener(biasNode, moduleData, &biasConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeAddAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeAddAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeAddAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeAddAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);
}

static void caerInputDVS132SRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	*out = caerDeviceDataGet(moduleData->moduleState);

	if (*out != NULL) {
		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindValidEventByTypeConst((caerSpecialEventPacketConst) special, TIMESTAMP_RESET)
				   != NULL)) {
			caerMainloopModuleResetOutputRevDeps(moduleData->moduleID);

			// Update master/slave information.
			struct caer_dvs132s_info devInfo = caerDVS132SInfoGet(moduleData->moduleState);

			sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
			sshsNodeUpdateReadOnlyAttribute(sourceInfoNode, "deviceIsMaster", SSHS_BOOL,
				(union sshs_node_attr_value){.boolean = devInfo.deviceIsMaster});
		}
	}
}

static void caerInputDVS132SExit(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, "DVS132S/");

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");
	sshsNodeRemoveAttributeListener(biasNode, moduleData, &biasConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeRemoveAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeRemoveAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeRemoveAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeRemoveAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeRemoveAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	// Remove statistics read modifiers.
	sshsNode statNode = sshsGetRelativeNode(deviceConfigNode, "statistics/");
	sshsAttributeUpdaterRemoveAllForNode(statNode);

	caerDeviceDataStop(moduleData->moduleState);

	caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices
		// detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void createDefaultBiasConfiguration(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, "DVS132S/");

	// Chip biases, based on testing defaults.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	sshsNodeCreateBool(biasNode, "BiasEnable", true, SSHS_FLAGS_NORMAL, "Enable bias generator to power chip.");

	sshsNodeCreateInt(
		biasNode, "PrBp", 100 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL, "Bias PrBp (in pAmp) - Photoreceptor bandwidth.");

	sshsNodeCreateInt(
		biasNode, "PrSFBpCoarse", 1, 0, 1023, SSHS_FLAGS_NORMAL, "Bias PrSFBp (in pAmp) - Photoreceptor bandwidth.");
	sshsNodeCreateInt(
		biasNode, "PrSFBpFine", 1, 0, 1023, SSHS_FLAGS_NORMAL, "Bias PrSFBp (in pAmp) - Photoreceptor bandwidth.");

	sshsNodeCreateInt(
		biasNode, "BlPuBp", 0, 0, 1000000, SSHS_FLAGS_NORMAL, "Bias BlPuBp (in pAmp) - Bitline pull-up strength.");
	sshsNodeCreateInt(biasNode, "BiasBufBp", 10 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL,
		"Bias BiasBufBp (in pAmp) - P type bias buffer strength.");
	sshsNodeCreateInt(
		biasNode, "OffBn", 200, 0, 1000000, SSHS_FLAGS_NORMAL, "Bias OffBn (in pAmp) - Comparator OFF threshold.");
	sshsNodeCreateInt(biasNode, "DiffBn", 10 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL,
		"Bias DiffBn (in pAmp) - Delta amplifier strength.");
	sshsNodeCreateInt(
		biasNode, "OnBn", 400 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL, "Bias OnBn (in pAmp) - Comparator ON threshold.");
	sshsNodeCreateInt(biasNode, "CasBn", 400 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL,
		"Bias CasBn (in pAmp) - Cascode for delta amplifier and comparator.");
	sshsNodeCreateInt(biasNode, "DPBn", 100 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL,
		"Bias DPBn (in pAmp) - In-pixel direct path current limit.");
	sshsNodeCreateInt(biasNode, "BiasBufBn", 10 * 1000, 0, 1000000, SSHS_FLAGS_NORMAL,
		"Bias BiasBufBn (in pAmp) - N type bias buffer strength.");
	sshsNodeCreateInt(biasNode, "ABufBn", 0, 0, 1000000, SSHS_FLAGS_NORMAL,
		"Bias ABufBn (in pAmp) - Diagnostic analog buffer strength.");
}

static void createDefaultLogicConfiguration(caerModuleData moduleData, const struct caer_dvs132s_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, "DVS132S/");

	// Subsystem 0: Multiplexer
	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");

	sshsNodeCreateBool(muxNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable multiplexer state machine.");
	sshsNodeCreateBool(muxNode, "TimestampRun", true, SSHS_FLAGS_NORMAL, "Enable µs-timestamp generation.");
	sshsNodeCreateBool(muxNode, "TimestampReset", false, SSHS_FLAGS_NOTIFY_ONLY, "Reset timestamps to zero.");
	sshsNodeCreateBool(muxNode, "RunChip", true, SSHS_FLAGS_NORMAL, "Enable the chip's bias generator.");
	sshsNodeCreateBool(
		muxNode, "DropDVSOnTransferStall", false, SSHS_FLAGS_NORMAL, "Drop Polarity events when USB FIFO is full.");
	sshsNodeCreateBool(muxNode, "DropExtInputOnTransferStall", true, SSHS_FLAGS_NORMAL,
		"Drop ExternalInput events when USB FIFO is full.");

	// Subsystem 1: DVS
	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");

	sshsNodeCreateBool(dvsNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable DVS (Polarity events).");
	sshsNodeCreateBool(dvsNode, "WaitOnTransferStall", true, SSHS_FLAGS_NORMAL, "On event FIFO full, pause readout.");
	sshsNodeCreateBool(dvsNode, "FilterAtLeast2Unsigned", false, SSHS_FLAGS_NORMAL,
		"Only read events from a group of four pixels if at least two are active, regardless of polarity.");
	sshsNodeCreateBool(dvsNode, "FilterNotAll4Unsigned", false, SSHS_FLAGS_NORMAL,
		"Only read events from a group of four pixels if not all four are active, regardless of polarity.");
	sshsNodeCreateBool(dvsNode, "FilterAtLeast2Signed", false, SSHS_FLAGS_NORMAL,
		"Only read events from a group of four pixels if at least two are active and have the same polarity.");
	sshsNodeCreateBool(dvsNode, "FilterNotAll4Signed", false, SSHS_FLAGS_NORMAL,
		"Only read events from a group of four pixels if not all four are active and have the same polarity.");
	sshsNodeCreateInt(
		dvsNode, "RestartTime", 100, 1, ((0x01 << 7) - 1), SSHS_FLAGS_NORMAL, "Restart pulse length, in us.");
	sshsNodeCreateInt(dvsNode, "CaptureInterval", 500, 1, ((0x01 << 21) - 1), SSHS_FLAGS_NORMAL,
		"Time interval between DVS readouts, in us.");
	sshsNodeCreateString(dvsNode, "RowEnable", "111111111111111111111111111111111111111111111111111111111111111111", 66,
		66, SSHS_FLAGS_NORMAL, "Enable rows to be read-out (ROI filter).");
	sshsNodeCreateString(dvsNode, "ColumnEnable", "1111111111111111111111111111111111111111111111111111", 52, 52,
		SSHS_FLAGS_NORMAL, "Enable columns to be read-out (ROI filter).");

	// Subsystem 3: IMU
	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");

	sshsNodeCreateBool(imuNode, "RunAccelerometer", true, SSHS_FLAGS_NORMAL, "Enable accelerometer.");
	sshsNodeCreateBool(imuNode, "RunGyroscope", true, SSHS_FLAGS_NORMAL, "Enable gyroscope.");
	sshsNodeCreateBool(imuNode, "RunTemperature", true, SSHS_FLAGS_NORMAL, "Enable temperature sensor.");
	sshsNodeCreateInt(imuNode, "AccelDataRate", 6, 0, 7, SSHS_FLAGS_NORMAL, "Accelerometer bandwidth configuration.");
	sshsNodeCreateInt(imuNode, "AccelFilter", 2, 0, 2, SSHS_FLAGS_NORMAL, "Accelerometer filter configuration.");
	sshsNodeCreateInt(imuNode, "AccelRange", 1, 0, 3, SSHS_FLAGS_NORMAL, "Accelerometer range configuration.");
	sshsNodeCreateInt(imuNode, "GyroDataRate", 5, 0, 7, SSHS_FLAGS_NORMAL, "Gyroscope bandwidth configuration.");
	sshsNodeCreateInt(imuNode, "GyroFilter", 2, 0, 2, SSHS_FLAGS_NORMAL, "Gyroscope filter configuration.");
	sshsNodeCreateInt(imuNode, "GyroRange", 2, 0, 4, SSHS_FLAGS_NORMAL, "Gyroscope range configuration.");

	// Subsystem 4: External Input
	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");

	sshsNodeCreateBool(extNode, "RunDetector", false, SSHS_FLAGS_NORMAL, "Enable signal detector 0.");
	sshsNodeCreateBool(
		extNode, "DetectRisingEdges", false, SSHS_FLAGS_NORMAL, "Emit special event if a rising edge is detected.");
	sshsNodeCreateBool(
		extNode, "DetectFallingEdges", false, SSHS_FLAGS_NORMAL, "Emit special event if a falling edge is detected.");
	sshsNodeCreateBool(extNode, "DetectPulses", true, SSHS_FLAGS_NORMAL, "Emit special event if a pulse is detected.");
	sshsNodeCreateBool(
		extNode, "DetectPulsePolarity", true, SSHS_FLAGS_NORMAL, "Polarity of the pulse to be detected.");
	sshsNodeCreateInt(extNode, "DetectPulseLength", 10, 1, ((0x01 << 20) - 1), SSHS_FLAGS_NORMAL,
		"Minimal length of the pulse to be detected (in µs).");

	if (devInfo->extInputHasGenerator) {
		sshsNodeCreateBool(extNode, "RunGenerator", false, SSHS_FLAGS_NORMAL, "Enable signal generator (PWM-like).");
		sshsNodeCreateBool(
			extNode, "GeneratePulsePolarity", true, SSHS_FLAGS_NORMAL, "Polarity of the generated pulse.");
		sshsNodeCreateInt(extNode, "GeneratePulseInterval", 10, 1, ((0x01 << 20) - 1), SSHS_FLAGS_NORMAL,
			"Time interval between consecutive pulses (in µs).");
		sshsNodeCreateInt(extNode, "GeneratePulseLength", 5, 1, ((0x01 << 20) - 1), SSHS_FLAGS_NORMAL,
			"Time length of a pulse (in µs).");
		sshsNodeCreateBool(extNode, "GenerateInjectOnRisingEdge", false, SSHS_FLAGS_NORMAL,
			"Emit a special event when a rising edge is generated.");
		sshsNodeCreateBool(extNode, "GenerateInjectOnFallingEdge", false, SSHS_FLAGS_NORMAL,
			"Emit a special event when a falling edge is generated.");
	}

	// Device event statistics.
	if (devInfo->muxHasStatistics) {
		sshsNode statNode = sshsGetRelativeNode(deviceConfigNode, "statistics/");

		sshsNodeCreateLong(statNode, "muxDroppedDVS", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped DVS events due to USB full.");
		sshsAttributeUpdaterAdd(statNode, "muxDroppedDVS", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "muxDroppedExtInput", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped External Input events due to USB full.");
		sshsAttributeUpdaterAdd(statNode, "muxDroppedExtInput", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);
	}

	if (devInfo->dvsHasStatistics) {
		sshsNode statNode = sshsGetRelativeNode(deviceConfigNode, "statistics/");

		sshsNodeCreateLong(statNode, "dvsTransactionsSuccess", 0, 0, INT64_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of groups of events received successfully.");
		sshsAttributeUpdaterAdd(
			statNode, "dvsTransactionsSuccess", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "dvsTransactionsSkipped", 0, 0, INT64_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of dropped groups of events due to full buffers.");
		sshsAttributeUpdaterAdd(
			statNode, "dvsTransactionsSkipped", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "dvsTransactionsAll", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped groups of events due to full buffers.");
		sshsAttributeUpdaterAdd(statNode, "dvsTransactionsAll", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "dvsTransactionsErrored", 0, 0, INT64_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of erroneous groups of events.");
		sshsAttributeUpdaterAdd(
			statNode, "dvsTransactionsErrored", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);
	}
}

static void createDefaultUSBConfiguration(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, "DVS132S/");

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

static void sendDefaultConfiguration(caerModuleData moduleData, const struct caer_dvs132s_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, "DVS132S/");

	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(deviceConfigNode, "bias/"), moduleData);

	// Wait 200 ms for biases to stabilize.
	struct timespec biasEnSleep = {.tv_sec = 0, .tv_nsec = 200000000};
	thrd_sleep(&biasEnSleep, NULL);

	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	usbConfigSend(sshsGetRelativeNode(deviceConfigNode, "usb/"), moduleData);
	muxConfigSend(sshsGetRelativeNode(deviceConfigNode, "multiplexer/"), moduleData);

	// Wait 50 ms for data transfer to be ready.
	struct timespec noDataSleep = {.tv_sec = 0, .tv_nsec = 50000000};
	thrd_sleep(&noDataSleep, NULL);

	dvsConfigSend(sshsGetRelativeNode(deviceConfigNode, "dvs/"), moduleData);
	imuConfigSend(sshsGetRelativeNode(deviceConfigNode, "imu/"), moduleData);
	extInputConfigSend(sshsGetRelativeNode(deviceConfigNode, "externalInput/"), moduleData, devInfo);
}

static void biasConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_PRBP,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "PrBp")))));

	struct caer_bias_coarsefine1024 prSF;
	prSF.coarseValue = U16T(sshsNodeGetInt(node, "PrSFBpCoarse"));
	prSF.fineValue   = U16T(sshsNodeGetInt(node, "PrSFBpFine"));
	caerDeviceConfigSet(
		moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_PRSFBP, caerBiasCoarseFine1024Generate(prSF));

	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_BLPUBP,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "BlPuBp")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_BIASBUFBP,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "BiasBufBp")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_CASBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "CasBn")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_DPBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "DPBn")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_BIASBUFBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "BiasBufBn")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_ABUFBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "ABufBn")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_OFFBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "OffBn")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_DIFFBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "DiffBn")))));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_ONBN,
		caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(sshsNodeGetInt(node, "OnBn")))));

	caerDeviceConfigSet(
		moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_RUN_CHIP, sshsNodeGetBool(node, "BiasEnable"));
}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "PrBp")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_PRBP,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PrSFBpCoarse")) {
			struct caer_bias_coarsefine1024 prSF;
			prSF.coarseValue = U16T(changeValue.iint);
			prSF.fineValue   = U16T(sshsNodeGetInt(node, "PrSFBpFine"));
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_PRSFBP,
				caerBiasCoarseFine1024Generate(prSF));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PrSFBpFine")) {
			struct caer_bias_coarsefine1024 prSF;
			prSF.coarseValue = U16T(sshsNodeGetInt(node, "PrSFBpCoarse"));
			prSF.fineValue   = U16T(changeValue.iint);
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_PRSFBP,
				caerBiasCoarseFine1024Generate(prSF));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BlPuBp")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_BLPUBP,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BiasBufBp")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_BIASBUFBP,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "OffBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_OFFBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DiffBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_DIFFBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "OnBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_ONBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "CasBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_CASBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DPBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_DPBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BiasBufBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_BIASBUFBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ABufBn")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_BIAS, DVS132S_CONFIG_BIAS_ABUFBN,
				caerBiasCoarseFine1024Generate(caerBiasCoarseFine1024FromCurrent(U32T(changeValue.iint))));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "BiasEnable")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_RUN_CHIP, changeValue.boolean);
		}
	}
}

static void muxConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_TIMESTAMP_RESET,
		sshsNodeGetBool(node, "TimestampReset"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_DROP_DVS_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropDVSOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_DROP_EXTINPUT_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropExtInputOnTransferStall"));
	caerDeviceConfigSet(
		moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_RUN_CHIP, sshsNodeGetBool(node, "RunChip"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_TIMESTAMP_RUN,
		sshsNodeGetBool(node, "TimestampRun"));
	caerDeviceConfigSet(
		moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_RUN, sshsNodeGetBool(node, "Run"));
}

static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampReset")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_TIMESTAMP_RESET, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropDVSOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_MUX,
				DVS132S_CONFIG_MUX_DROP_DVS_ON_TRANSFER_STALL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropExtInputOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_MUX,
				DVS132S_CONFIG_MUX_DROP_EXTINPUT_ON_TRANSFER_STALL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunChip")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_RUN_CHIP, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampRun")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_TIMESTAMP_RUN, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_RUN, changeValue.boolean);
		}
	}
}

static inline void dvsRowEnableParse(const char *rowEnableStr, caerDeviceHandle cdh) {
	size_t rowEnableIndex = 0;

	uint32_t rowInt31To0 = 0;

	for (size_t i = 0; i < 32; i++) {
		if (rowEnableStr[rowEnableIndex++] == '1') {
			rowInt31To0 |= U32T(0x01 << i);
		}
	}

	uint32_t rowInt63To32 = 0;

	for (size_t i = 0; i < 32; i++) {
		if (rowEnableStr[rowEnableIndex++] == '1') {
			rowInt63To32 |= U32T(0x01 << i);
		}
	}

	uint32_t rowInt65To64 = 0;

	for (size_t i = 0; i < 2; i++) {
		if (rowEnableStr[rowEnableIndex++] == '1') {
			rowInt65To64 |= U32T(0x01 << i);
		}
	}

	caerDeviceConfigSet(cdh, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_ROW_ENABLE_31_TO_0, rowInt31To0);
	caerDeviceConfigSet(cdh, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_ROW_ENABLE_63_TO_32, rowInt63To32);
	caerDeviceConfigSet(cdh, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_ROW_ENABLE_65_TO_64, rowInt65To64);
}

static inline void dvsColumnEnableParse(const char *columnEnableStr, caerDeviceHandle cdh) {
	size_t columnEnableIndex = 0;

	uint32_t columnInt31To0 = 0;

	for (size_t i = 0; i < 32; i++) {
		if (columnEnableStr[columnEnableIndex++] == '1') {
			columnInt31To0 |= U32T(0x01 << i);
		}
	}

	uint32_t columnInt51To32 = 0;

	for (size_t i = 0; i < 20; i++) {
		if (columnEnableStr[columnEnableIndex++] == '1') {
			columnInt51To32 |= U32T(0x01 << i);
		}
	}

	caerDeviceConfigSet(cdh, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_COLUMN_ENABLE_31_TO_0, columnInt31To0);
	caerDeviceConfigSet(cdh, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_COLUMN_ENABLE_51_TO_32, columnInt51To32);
}

static void dvsConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_WAIT_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "WaitOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_FILTER_AT_LEAST_2_UNSIGNED,
		sshsNodeGetBool(node, "FilterAtLeast2Unsigned"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_FILTER_NOT_ALL_4_UNSIGNED,
		sshsNodeGetBool(node, "FilterNotAll4Unsigned"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_FILTER_AT_LEAST_2_SIGNED,
		sshsNodeGetBool(node, "FilterAtLeast2Signed"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_FILTER_NOT_ALL_4_SIGNED,
		sshsNodeGetBool(node, "FilterNotAll4Signed"));

	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_RESTART_TIME,
		U32T(sshsNodeGetInt(node, "RestartTime")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_CAPTURE_INTERVAL,
		U32T(sshsNodeGetInt(node, "CaptureInterval")));

	// Parse string bitfields into corresponding integer bitfields for device.
	char *rowEnableStr = sshsNodeGetString(node, "RowEnable");

	dvsRowEnableParse(rowEnableStr, moduleData->moduleState);

	free(rowEnableStr);

	// Parse string bitfields into corresponding integer bitfields for device.
	char *columnEnableStr = sshsNodeGetString(node, "ColumnEnable");

	dvsColumnEnableParse(columnEnableStr, moduleData->moduleState);

	free(columnEnableStr);

	// Wait 5 ms for row/column enables to have been sent out.
	struct timespec enableSleep = {.tv_sec = 0, .tv_nsec = 5000000};
	thrd_sleep(&enableSleep, NULL);

	caerDeviceConfigSet(
		moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_RUN, sshsNodeGetBool(node, "Run"));
}

static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "WaitOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_WAIT_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterAtLeast2Unsigned")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS,
				DVS132S_CONFIG_DVS_FILTER_AT_LEAST_2_UNSIGNED, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterNotAll4Unsigned")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS,
				DVS132S_CONFIG_DVS_FILTER_NOT_ALL_4_UNSIGNED, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterAtLeast2Signed")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS,
				DVS132S_CONFIG_DVS_FILTER_AT_LEAST_2_SIGNED, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterNotAll4Signed")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_FILTER_NOT_ALL_4_SIGNED,
				changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "RestartTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_RESTART_TIME, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "CaptureInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_CAPTURE_INTERVAL,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_STRING && caerStrEquals(changeKey, "RowEnable")) {
			// Parse string bitfields into corresponding integer bitfields for device.
			const char *rowEnableStr = changeValue.string;

			dvsRowEnableParse(rowEnableStr, moduleData->moduleState);
		}
		else if (changeType == SSHS_STRING && caerStrEquals(changeKey, "ColumnEnable")) {
			// Parse string bitfields into corresponding integer bitfields for device.
			const char *columnEnableStr = changeValue.string;

			dvsColumnEnableParse(columnEnableStr, moduleData->moduleState);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_RUN, changeValue.boolean);
		}
	}
}

static void imuConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_ACCEL_DATA_RATE,
		U32T(sshsNodeGetInt(node, "AccelDataRate")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_ACCEL_FILTER,
		U32T(sshsNodeGetInt(node, "AccelFilter")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_ACCEL_RANGE,
		U32T(sshsNodeGetInt(node, "AccelRange")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_GYRO_DATA_RATE,
		U32T(sshsNodeGetInt(node, "GyroDataRate")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_GYRO_FILTER,
		U32T(sshsNodeGetInt(node, "GyroFilter")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_GYRO_RANGE,
		U32T(sshsNodeGetInt(node, "GyroRange")));

	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_RUN_ACCELEROMETER,
		sshsNodeGetBool(node, "RunAccelerometer"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_RUN_GYROSCOPE,
		sshsNodeGetBool(node, "RunGyroscope"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_RUN_TEMPERATURE,
		sshsNodeGetBool(node, "RunTemperature"));
}

static void imuConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "AccelDataRate")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_ACCEL_DATA_RATE,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AccelFilter")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_ACCEL_FILTER, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AccelRange")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_ACCEL_RANGE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GyroDataRate")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_GYRO_DATA_RATE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GyroFilter")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_GYRO_FILTER, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GyroRange")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_GYRO_RANGE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunAccelerometer")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_RUN_ACCELEROMETER, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunGyroscope")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_RUN_GYROSCOPE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunTemperature")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_IMU, DVS132S_CONFIG_IMU_RUN_TEMPERATURE, changeValue.boolean);
		}
	}
}

static void extInputConfigSend(sshsNode node, caerModuleData moduleData, const struct caer_dvs132s_info *devInfo) {
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_DETECT_RISING_EDGES,
		sshsNodeGetBool(node, "DetectRisingEdges"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_DETECT_FALLING_EDGES,
		sshsNodeGetBool(node, "DetectFallingEdges"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_DETECT_PULSES,
		sshsNodeGetBool(node, "DetectPulses"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY,
		sshsNodeGetBool(node, "DetectPulsePolarity"));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH,
		U32T(sshsNodeGetInt(node, "DetectPulseLength")));
	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_RUN_DETECTOR,
		sshsNodeGetBool(node, "RunDetector"));

	if (devInfo->extInputHasGenerator) {
		caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
			DVS132S_CONFIG_EXTINPUT_GENERATE_PULSE_POLARITY, sshsNodeGetBool(node, "GeneratePulsePolarity"));
		caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
			DVS132S_CONFIG_EXTINPUT_GENERATE_PULSE_INTERVAL, U32T(sshsNodeGetInt(node, "GeneratePulseInterval")));
		caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
			DVS132S_CONFIG_EXTINPUT_GENERATE_PULSE_LENGTH, U32T(sshsNodeGetInt(node, "GeneratePulseLength")));
		caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
			DVS132S_CONFIG_EXTINPUT_GENERATE_INJECT_ON_RISING_EDGE,
			sshsNodeGetBool(node, "GenerateInjectOnRisingEdge"));
		caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
			DVS132S_CONFIG_EXTINPUT_GENERATE_INJECT_ON_FALLING_EDGE,
			sshsNodeGetBool(node, "GenerateInjectOnFallingEdge"));
		caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_RUN_GENERATOR,
			sshsNodeGetBool(node, "RunGenerator"));
	}
}

static void extInputConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectRisingEdges")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_DETECT_RISING_EDGES, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectFallingEdges")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_DETECT_FALLING_EDGES, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulses")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_DETECT_PULSES,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulsePolarity")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DetectPulseLength")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunDetector")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_RUN_DETECTOR,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GeneratePulsePolarity")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_GENERATE_PULSE_POLARITY, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GeneratePulseInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_GENERATE_PULSE_INTERVAL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GeneratePulseLength")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_GENERATE_PULSE_LENGTH, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateInjectOnRisingEdge")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_GENERATE_INJECT_ON_RISING_EDGE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateInjectOnFallingEdge")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT,
				DVS132S_CONFIG_EXTINPUT_GENERATE_INJECT_ON_FALLING_EDGE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunGenerator")) {
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_EXTINPUT, DVS132S_CONFIG_EXTINPUT_RUN_GENERATOR,
				changeValue.boolean);
		}
	}
}

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
		U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
		U32T(sshsNodeGetInt(node, "BufferSize")));

	caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_USB, DVS132S_CONFIG_USB_EARLY_PACKET_DELAY,
		U32T(sshsNodeGetInt(node, "EarlyPacketDelay")));
	caerDeviceConfigSet(
		moduleData->moduleState, DVS132S_CONFIG_USB, DVS132S_CONFIG_USB_RUN, sshsNodeGetBool(node, "Run"));
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
			caerDeviceConfigSet(moduleData->moduleState, DVS132S_CONFIG_USB, DVS132S_CONFIG_USB_EARLY_PACKET_DELAY,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DVS132S_CONFIG_USB, DVS132S_CONFIG_USB_RUN, changeValue.boolean);
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

static union sshs_node_attr_value statisticsUpdater(
	void *userData, const char *key, enum sshs_node_attr_value_type type) {
	UNUSED_ARGUMENT(type); // We know all statistics are always LONG.

	caerDeviceHandle handle = userData;

	union sshs_node_attr_value statisticValue = {.ilong = 0};

	if (caerStrEquals(key, "muxDroppedDVS")) {
		caerDeviceConfigGet64(
			handle, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_STATISTICS_DVS_DROPPED, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "muxDroppedExtInput")) {
		caerDeviceConfigGet64(handle, DVS132S_CONFIG_MUX, DVS132S_CONFIG_MUX_STATISTICS_EXTINPUT_DROPPED,
			(uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsTransactionsSuccess")) {
		caerDeviceConfigGet64(handle, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_STATISTICS_TRANSACTIONS_SUCCESS,
			(uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsTransactionsSkipped")) {
		caerDeviceConfigGet64(handle, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_STATISTICS_TRANSACTIONS_SKIPPED,
			(uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsTransactionsAll")) {
		uint64_t success = 0;
		caerDeviceConfigGet64(handle, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_STATISTICS_TRANSACTIONS_SUCCESS, &success);

		uint64_t skipped = 0;
		caerDeviceConfigGet64(handle, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_STATISTICS_TRANSACTIONS_SKIPPED, &skipped);

		statisticValue.ilong = I64T(success + skipped);
	}
	else if (caerStrEquals(key, "dvsTransactionsErrored")) {
		uint32_t statisticValue32 = 0;
		caerDeviceConfigGet(
			handle, DVS132S_CONFIG_DVS, DVS132S_CONFIG_DVS_STATISTICS_TRANSACTIONS_ERRORED, &statisticValue32);
		statisticValue.ilong = statisticValue32;
	}

	return (statisticValue);
}
