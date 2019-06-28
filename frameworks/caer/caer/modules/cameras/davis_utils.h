#ifndef MODULES_INI_DAVIS_UTILS_H_
#define MODULES_INI_DAVIS_UTILS_H_

#include <libcaer/events/frame.h>
#include <libcaer/events/imu6.h>
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/special.h>

#include <libcaer/devices/davis.h>

#include "caer-sdk/mainloop.h"

static void caerInputDAVISCommonSystemConfigInit(sshsNode moduleNode);
static void caerInputDAVISCommonInit(caerModuleData moduleData, struct caer_davis_info *devInfo);
static void caerInputDAVISCommonRun(
	caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void moduleShutdownNotify(void *p);

static void createDefaultBiasConfiguration(caerModuleData moduleData, const char *nodePrefix, int16_t chipID);
static void createDefaultLogicConfiguration(
	caerModuleData moduleData, const char *nodePrefix, struct caer_davis_info *devInfo);

static void biasConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void chipConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void chipConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void muxConfigSend(sshsNode node, caerModuleData moduleData);
static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void dvsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void apsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void apsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void imuConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void imuConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void extInputConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void extInputConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigSend(sshsNode node, caerModuleData moduleData);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

static union sshs_node_attr_value statisticsUpdater(
	void *userData, const char *key, enum sshs_node_attr_value_type type);
static union sshs_node_attr_value apsExposureUpdater(
	void *userData, const char *key, enum sshs_node_attr_value_type type);

static void createVDACBiasSetting(sshsNode biasNode, const char *biasName, uint8_t voltageValue, uint8_t currentValue);
static uint16_t generateVDACBiasParent(sshsNode biasNode, const char *biasName);
static uint16_t generateVDACBias(sshsNode biasNode);
static void createCoarseFineBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
	bool enabled, const char *sex, const char *type);
static uint16_t generateCoarseFineBiasParent(sshsNode biasNode, const char *biasName);
static uint16_t generateCoarseFineBias(sshsNode biasNode);
static void createShiftedSourceBiasSetting(sshsNode biasNode, const char *biasName, uint8_t refValue, uint8_t regValue,
	const char *operatingMode, const char *voltageLevel);
static uint16_t generateShiftedSourceBiasParent(sshsNode biasNode, const char *biasName);
static uint16_t generateShiftedSourceBias(sshsNode biasNode);

static inline const char *chipIDToName(int16_t chipID, bool withEndSlash) {
	switch (chipID) {
		case 0:
			return ((withEndSlash) ? ("DAVIS240A/") : ("DAVIS240A"));
			break;

		case 1:
			return ((withEndSlash) ? ("DAVIS240B/") : ("DAVIS240B"));
			break;

		case 2:
			return ((withEndSlash) ? ("DAVIS240C/") : ("DAVIS240C"));
			break;

		case 3:
			return ((withEndSlash) ? ("DAVIS128/") : ("DAVIS128"));
			break;

		case 5: // DAVIS346B -> only FSI chip.
			return ((withEndSlash) ? ("DAVIS346/") : ("DAVIS346"));
			break;

		case 6:
			return ((withEndSlash) ? ("DAVIS640/") : ("DAVIS640"));
			break;

		case 7:
			return ((withEndSlash) ? ("DAVIS640H/") : ("DAVIS640H"));
			break;

		case 8: // PixelParade.
			return ((withEndSlash) ? ("DAVIS208/") : ("DAVIS208"));
			break;

		case 9: // DAVIS346Cbsi -> only BSI chip.
			return ((withEndSlash) ? ("DAVIS346BSI/") : ("DAVIS346BSI"));
			break;
	}

	return ((withEndSlash) ? ("Unsupported/") : ("Unsupported"));
}

static void caerInputDAVISCommonSystemConfigInit(sshsNode moduleNode) {
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

static void caerInputDAVISCommonInit(caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Put global source information into SSHS.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateInt(sourceInfoNode, "firmwareVersion", devInfo->firmwareVersion, devInfo->firmwareVersion,
		devInfo->firmwareVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device firmware version.");
	sshsNodeCreateInt(sourceInfoNode, "logicVersion", devInfo->logicVersion, devInfo->logicVersion,
		devInfo->logicVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device logic version.");

	sshsNodeCreateInt(sourceInfoNode, "chipID", devInfo->chipID, devInfo->chipID, devInfo->chipID,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device chip identification number.");
	sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", devInfo->deviceIsMaster,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Timestamp synchronization support: device master status.");

	sshsNodeCreateBool(sourceInfoNode, "muxHasStatistics", devInfo->muxHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA Multiplexer statistics (USB event drops).");

	sshsNodeCreateInt(sourceInfoNode, "polaritySizeX", devInfo->dvsSizeX, devInfo->dvsSizeX, devInfo->dvsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events width.");
	sshsNodeCreateInt(sourceInfoNode, "polaritySizeY", devInfo->dvsSizeY, devInfo->dvsSizeY, devInfo->dvsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events height.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasPixelFilter", devInfo->dvsHasPixelFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS Pixel-level filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasBackgroundActivityFilter", devInfo->dvsHasBackgroundActivityFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Device supports FPGA DVS Background-Activity and Refractory Period filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasROIFilter", devInfo->dvsHasROIFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS ROI filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasSkipFilter", devInfo->dvsHasSkipFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS skip events filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasPolarityFilter", devInfo->dvsHasPolarityFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS polarity filter.");
	sshsNodeCreateBool(sourceInfoNode, "dvsHasStatistics", devInfo->dvsHasStatistics,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports FPGA DVS statistics.");

	sshsNodeCreateInt(sourceInfoNode, "frameSizeX", devInfo->apsSizeX, devInfo->apsSizeX, devInfo->apsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events width.");
	sshsNodeCreateInt(sourceInfoNode, "frameSizeY", devInfo->apsSizeY, devInfo->apsSizeY, devInfo->apsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events height.");
	sshsNodeCreateInt(sourceInfoNode, "apsColorFilter", I8T(devInfo->apsColorFilter), I8T(devInfo->apsColorFilter),
		I8T(devInfo->apsColorFilter), SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "APS sensor color-filter pattern.");
	sshsNodeCreateBool(sourceInfoNode, "apsHasGlobalShutter", devInfo->apsHasGlobalShutter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "APS sensor supports global-shutter mode.");

	sshsNodeCreateBool(sourceInfoNode, "extInputHasGenerator", devInfo->extInputHasGenerator,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device supports generating pulses on output signal jack.");

	// Put source information for generic visualization, to be used to display and debug filter information.
	int16_t dataSizeX = (devInfo->dvsSizeX > devInfo->apsSizeX) ? (devInfo->dvsSizeX) : (devInfo->apsSizeX);
	int16_t dataSizeY = (devInfo->dvsSizeY > devInfo->apsSizeY) ? (devInfo->dvsSizeY) : (devInfo->apsSizeY);

	sshsNodeCreateInt(sourceInfoNode, "dataSizeX", dataSizeX, dataSizeX, dataSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateInt(sourceInfoNode, "dataSizeY", dataSizeY, dataSizeY, dataSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(
		NULL, 0, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID, chipIDToName(devInfo->chipID, false));

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(devInfo->chipID, false));
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
}

static void caerInputDAVISCommonRun(
	caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
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
			struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

			sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
			sshsNodeUpdateReadOnlyAttribute(sourceInfoNode, "deviceIsMaster", SSHS_BOOL,
				(union sshs_node_attr_value){.boolean = devInfo.deviceIsMaster});
		}
	}
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void createDefaultBiasConfiguration(caerModuleData moduleData, const char *nodePrefix, int16_t chipID) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, nodePrefix);

	// Chip biases, based on testing defaults.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	if (IS_DAVIS240(chipID)) {
		createCoarseFineBiasSetting(biasNode, "DiffBn", 4, 39, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OnBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OffBn", 4, 0, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsCasEpc", 5, 185, true, "N", "Cascode");
		createCoarseFineBiasSetting(biasNode, "DiffCasBnc", 5, 115, true, "N", "Cascode");
		createCoarseFineBiasSetting(biasNode, "ApsROSFBn", 6, 219, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "LocalBufBn", 5, 164, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PixInvBn", 5, 129, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrBp", 2, 58, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 16, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "RefrBp", 4, 25, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPdBn", 6, 91, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "LcolTimeoutBn", 5, 49, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuXBp", 4, 80, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuYBp", 7, 152, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFThrBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFRefrBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PadFollBn", 7, 215, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsOverflowLevelBn", 6, 253, true, "N", "Normal");

		createCoarseFineBiasSetting(biasNode, "BiasBuffer", 5, 254, true, "N", "Normal");

		createShiftedSourceBiasSetting(biasNode, "SSP", 1, 33, "ShiftedSource", "SplitGate");
		createShiftedSourceBiasSetting(biasNode, "SSN", 1, 33, "ShiftedSource", "SplitGate");
	}

	if (IS_DAVIS128(chipID) || IS_DAVIS208(chipID) || IS_DAVIS346(chipID) || IS_DAVIS640(chipID)) {
		// This is first so that it takes precedence over later settings for all other chips.
		if (IS_DAVIS640(chipID)) {
			// Slow down pixels for big 640x480 array, to avoid overwhelming the AER bus.
			createCoarseFineBiasSetting(biasNode, "PrBp", 2, 3, true, "P", "Normal");
			createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 1, true, "P", "Normal");
			createCoarseFineBiasSetting(biasNode, "OnBn", 5, 155, true, "N", "Normal");
			createCoarseFineBiasSetting(biasNode, "OffBn", 1, 4, true, "N", "Normal");

			createCoarseFineBiasSetting(biasNode, "BiasBuffer", 6, 125, true, "N", "Normal");
		}

		createVDACBiasSetting(biasNode, "ApsOverflowLevel", 27, 6);
		createVDACBiasSetting(biasNode, "ApsCas", 21, 6);
		createVDACBiasSetting(biasNode, "AdcRefHigh", 32, 7);
		createVDACBiasSetting(biasNode, "AdcRefLow", 1, 7);

		if (IS_DAVIS346(chipID) || IS_DAVIS640(chipID)) {
			// Only DAVIS346 and 640 have ADC testing.
			createVDACBiasSetting(biasNode, "AdcTestVoltage", 21, 7);
		}

		if (IS_DAVIS208(chipID)) {
			createVDACBiasSetting(biasNode, "ResetHighPass", 63, 7);
			createVDACBiasSetting(biasNode, "RefSS", 11, 5);

			createCoarseFineBiasSetting(biasNode, "RegBiasBp", 5, 20, true, "P", "Normal");
			createCoarseFineBiasSetting(biasNode, "RefSSBn", 5, 20, true, "N", "Normal");
		}

		createCoarseFineBiasSetting(biasNode, "LocalBufBn", 5, 164, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PadFollBn", 7, 215, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "DiffBn", 4, 39, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OnBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OffBn", 4, 1, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PixInvBn", 5, 129, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrBp", 2, 58, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 16, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "RefrBp", 4, 25, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ReadoutBufBp", 6, 20, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsROSFBn", 6, 219, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AdcCompBp", 5, 20, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ColSelLowBn", 0, 1, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "DACBufBp", 6, 60, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "LcolTimeoutBn", 5, 49, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPdBn", 6, 91, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuXBp", 4, 80, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuYBp", 7, 152, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFRefrBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFThrBn", 5, 255, true, "N", "Normal");

		createCoarseFineBiasSetting(biasNode, "BiasBuffer", 5, 254, true, "N", "Normal");

		createShiftedSourceBiasSetting(biasNode, "SSP", 1, 33, "ShiftedSource", "SplitGate");
		createShiftedSourceBiasSetting(biasNode, "SSN", 1, 33, "ShiftedSource", "SplitGate");
	}

	if (IS_DAVIS640H(chipID)) {
		createVDACBiasSetting(biasNode, "ApsCas", 21, 4);
		createVDACBiasSetting(biasNode, "OVG1Lo", 63, 4);
		createVDACBiasSetting(biasNode, "OVG2Lo", 0, 0);
		createVDACBiasSetting(biasNode, "TX2OVG2Hi", 63, 0);
		createVDACBiasSetting(biasNode, "Gnd07", 13, 4);
		createVDACBiasSetting(biasNode, "AdcTestVoltage", 21, 0);
		createVDACBiasSetting(biasNode, "AdcRefHigh", 46, 7);
		createVDACBiasSetting(biasNode, "AdcRefLow", 3, 7);

		createCoarseFineBiasSetting(biasNode, "IFRefrBn", 5, 255, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFThrBn", 5, 255, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "LocalBufBn", 5, 164, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PadFollBn", 7, 209, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PixInvBn", 4, 164, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "DiffBn", 3, 75, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OnBn", 6, 95, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OffBn", 2, 41, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrBp", 1, 88, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 173, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "RefrBp", 2, 62, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ArrayBiasBufferBn", 6, 128, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "ArrayLogicBufferBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "FalltimeBn", 7, 41, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "RisetimeBp", 6, 162, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ReadoutBufBp", 6, 20, false, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsROSFBn", 7, 82, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AdcCompBp", 4, 159, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "DACBufBp", 6, 194, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "LcolTimeoutBn", 5, 49, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPdBn", 6, 91, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuXBp", 4, 80, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuYBp", 7, 152, true, "P", "Normal");

		createCoarseFineBiasSetting(biasNode, "BiasBuffer", 6, 251, true, "N", "Normal");

		createShiftedSourceBiasSetting(biasNode, "SSP", 1, 33, "TiedToRail", "SplitGate");
		createShiftedSourceBiasSetting(biasNode, "SSN", 2, 33, "ShiftedSource", "SplitGate");
	}

	// Chip configuration shift register.
	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");

	sshsNodeCreateInt(chipNode, "DigitalMux0", 0, 0, 15, SSHS_FLAGS_NORMAL, "Digital debug multiplexer 0.");
	sshsNodeCreateInt(chipNode, "DigitalMux1", 0, 0, 15, SSHS_FLAGS_NORMAL, "Digital debug multiplexer 1.");
	sshsNodeCreateInt(chipNode, "DigitalMux2", 0, 0, 15, SSHS_FLAGS_NORMAL, "Digital debug multiplexer 2.");
	sshsNodeCreateInt(chipNode, "DigitalMux3", 0, 0, 15, SSHS_FLAGS_NORMAL, "Digital debug multiplexer 3.");
	sshsNodeCreateInt(chipNode, "AnalogMux0", 0, 0, 15, SSHS_FLAGS_NORMAL, "Analog debug multiplexer 0.");
	sshsNodeCreateInt(chipNode, "AnalogMux1", 0, 0, 15, SSHS_FLAGS_NORMAL, "Analog debug multiplexer 1.");
	sshsNodeCreateInt(chipNode, "AnalogMux2", 0, 0, 15, SSHS_FLAGS_NORMAL, "Analog debug multiplexer 2.");
	sshsNodeCreateInt(chipNode, "BiasMux0", 0, 0, 15, SSHS_FLAGS_NORMAL, "Bias debug multiplexer 0.");

	sshsNodeCreateBool(chipNode, "ResetCalibNeuron", true, SSHS_FLAGS_NORMAL,
		"Turn off the integrate and fire calibration neuron (bias generator).");
	sshsNodeCreateBool(chipNode, "TypeNCalibNeuron", false, SSHS_FLAGS_NORMAL,
		"Make the integrate and fire calibration neuron measure N-type biases; otherwise measures P-type biases.");
	sshsNodeCreateBool(chipNode, "ResetTestPixel", true, SSHS_FLAGS_NORMAL, "Keep the test pixel in reset (disabled).");
	sshsNodeCreateBool(chipNode, "AERnArow", false, SSHS_FLAGS_NORMAL, "Use nArow in the AER state machine.");
	sshsNodeCreateBool(
		chipNode, "UseAOut", false, SSHS_FLAGS_NORMAL, "Enable analog pads for the analog debug multiplexers outputs.");

	// No GlobalShutter flag here, it's controlled by the APS module's GS flag, and libcaer
	// ensures that both the chip SR and the APS module flags are kept in sync.

	if (IS_DAVIS240A(chipID) || IS_DAVIS240B(chipID)) {
		sshsNodeCreateBool(chipNode, "SpecialPixelControl", false, SSHS_FLAGS_NORMAL,
			IS_DAVIS240A(chipID) ? ("Enable experimental hot-pixels suppression circuit.")
								 : ("Enable experimental pixel stripes on right side of array."));
	}

	if (IS_DAVIS128(chipID) || IS_DAVIS208(chipID) || IS_DAVIS346(chipID) || IS_DAVIS640(chipID)
		|| IS_DAVIS640H(chipID)) {
		sshsNodeCreateBool(chipNode, "SelectGrayCounter", 1, SSHS_FLAGS_NORMAL,
			"Select which gray counter to use with the internal ADC: '0' means the external gray counter "
			"is used, which has to be supplied off-chip. '1' means the on-chip gray counter is used instead.");
	}

	if (IS_DAVIS346(chipID) || IS_DAVIS640(chipID) || IS_DAVIS640H(chipID)) {
		sshsNodeCreateBool(chipNode, "TestADC", false, SSHS_FLAGS_NORMAL,
			"Test ADC functionality: if true, the ADC takes its input voltage not from the pixel, but from the "
			"VDAC 'AdcTestVoltage'. If false, the voltage comes from the pixels.");
	}

	if (IS_DAVIS208(chipID)) {
		sshsNodeCreateBool(chipNode, "SelectPreAmpAvg", false, SSHS_FLAGS_NORMAL,
			"If 1, connect PreAmpAvgxA to calibration neuron, if 0, commongate.");
		sshsNodeCreateBool(
			chipNode, "SelectBiasRefSS", false, SSHS_FLAGS_NORMAL, "If 1, select Nbias Blk1N, if 0, VDAC VblkV2.");
		sshsNodeCreateBool(chipNode, "SelectSense", true, SSHS_FLAGS_NORMAL, "Enable Sensitive pixels.");
		sshsNodeCreateBool(chipNode, "SelectPosFb", false, SSHS_FLAGS_NORMAL, "Enable PosFb pixels.");
		sshsNodeCreateBool(chipNode, "SelectHighPass", false, SSHS_FLAGS_NORMAL, "Enable HighPass pixels.");
	}

	if (IS_DAVIS640H(chipID)) {
		sshsNodeCreateBool(chipNode, "AdjustOVG1Lo", true, SSHS_FLAGS_NORMAL, "Adjust OVG1 Low.");
		sshsNodeCreateBool(chipNode, "AdjustOVG2Lo", false, SSHS_FLAGS_NORMAL, "Adjust OVG2 Low.");
		sshsNodeCreateBool(chipNode, "AdjustTX2OVG2Hi", false, SSHS_FLAGS_NORMAL, "Adjust TX2OVG2Hi.");
	}
}

static void createDefaultLogicConfiguration(
	caerModuleData moduleData, const char *nodePrefix, struct caer_davis_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, nodePrefix);

	// Subsystem 0: Multiplexer
	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");

	sshsNodeCreateBool(muxNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable multiplexer state machine.");
	sshsNodeCreateBool(muxNode, "TimestampRun", true, SSHS_FLAGS_NORMAL, "Enable µs-timestamp generation.");
	sshsNodeCreateBool(muxNode, "TimestampReset", false, SSHS_FLAGS_NOTIFY_ONLY, "Reset timestamps to zero.");
	sshsNodeCreateBool(muxNode, "RunChip", true, SSHS_FLAGS_NORMAL, "Enable the chip's bias generator.");
	sshsNodeCreateBool(muxNode, "DropExtInputOnTransferStall", true, SSHS_FLAGS_NORMAL,
		"Drop ExternalInput events when USB FIFO is full.");
	sshsNodeCreateBool(
		muxNode, "DropDVSOnTransferStall", true, SSHS_FLAGS_NORMAL, "Drop Polarity events when USB FIFO is full.");

	// Subsystem 1: DVS AER
	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");

	sshsNodeCreateBool(dvsNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable DVS (Polarity events).");
	sshsNodeCreateBool(dvsNode, "WaitOnTransferStall", false, SSHS_FLAGS_NORMAL,
		"On event FIFO full, wait to ACK until again empty if true, or just continue ACKing if false.");
	sshsNodeCreateBool(dvsNode, "ExternalAERControl", false, SSHS_FLAGS_NORMAL,
		"Don't drive AER ACK pin from FPGA (dvs.Run must also be disabled).");

	if (devInfo->dvsHasPixelFilter) {
		sshsNodeCreateInt(dvsNode, "FilterPixel0Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 0 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel0Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 0 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel1Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 1 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel1Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 1 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel2Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 2 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel2Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 2 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel3Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 3 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel3Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 3 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel4Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 4 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel4Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 4 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel5Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 5 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel5Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 5 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel6Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 6 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel6Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 6 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel7Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL,
			"Row/Y address of pixel 7 to filter out.");
		sshsNodeCreateInt(dvsNode, "FilterPixel7Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL,
			"Column/X address of pixel 7 to filter out.");
		sshsNodeCreateBool(dvsNode, "FilterPixelAutoTrain", false, SSHS_FLAGS_NOTIFY_ONLY,
			"Set hardware pixel filter up automatically using software hot-pixel detection.");
	}

	if (devInfo->dvsHasBackgroundActivityFilter) {
		sshsNodeCreateBool(dvsNode, "FilterBackgroundActivity", true, SSHS_FLAGS_NORMAL,
			"Filter background events using hardware filter on FPGA.");
		sshsNodeCreateInt(dvsNode, "FilterBackgroundActivityTime", 8, 0, (0x01 << 12) - 1, SSHS_FLAGS_NORMAL,
			"Maximum time difference for events to be considered correlated and not be filtered out (in 250µs units).");
		sshsNodeCreateBool(dvsNode, "FilterRefractoryPeriod", false, SSHS_FLAGS_NORMAL,
			"Limit pixel firing rate using hardware filter on FPGA.");
		sshsNodeCreateInt(dvsNode, "FilterRefractoryPeriodTime", 1, 0, (0x01 << 12) - 1, SSHS_FLAGS_NORMAL,
			"Minimum time between events to not be filtered out (in 250µs units).");
	}

	if (devInfo->dvsHasROIFilter) {
		sshsNodeCreateInt(dvsNode, "FilterROIStartColumn", 0, 0, I16T(devInfo->dvsSizeX - 1), SSHS_FLAGS_NORMAL,
			"Column/X address of ROI filter start point.");
		sshsNodeCreateInt(dvsNode, "FilterROIStartRow", 0, 0, I16T(devInfo->dvsSizeY - 1), SSHS_FLAGS_NORMAL,
			"Row/Y address of ROI filter start point.");
		sshsNodeCreateInt(dvsNode, "FilterROIEndColumn", I16T(devInfo->dvsSizeX - 1), 0, I16T(devInfo->dvsSizeX - 1),
			SSHS_FLAGS_NORMAL, "Column/X address of ROI filter end point.");
		sshsNodeCreateInt(dvsNode, "FilterROIEndRow", I16T(devInfo->dvsSizeY - 1), 0, I16T(devInfo->dvsSizeY - 1),
			SSHS_FLAGS_NORMAL, "Row/Y address of ROI filter end point.");
	}

	if (devInfo->dvsHasSkipFilter) {
		sshsNodeCreateBool(dvsNode, "FilterSkipEvents", false, SSHS_FLAGS_NORMAL, "Skip one event every N.");
		sshsNodeCreateInt(dvsNode, "FilterSkipEventsEvery", 1, 1, (0x01 << 8) - 1, SSHS_FLAGS_NORMAL,
			"Number of events to let through before skipping one.");
	}

	if (devInfo->dvsHasPolarityFilter) {
		sshsNodeCreateBool(
			dvsNode, "FilterPolarityFlatten", false, SSHS_FLAGS_NORMAL, "Change all event polarities to OFF.");
		sshsNodeCreateBool(
			dvsNode, "FilterPolaritySuppress", false, SSHS_FLAGS_NORMAL, "Suppress events of a certain polarity.");
		sshsNodeCreateBool(dvsNode, "FilterPolaritySuppressType", false, SSHS_FLAGS_NORMAL,
			"Polarity to suppress (false=OFF, true=ON).");
	}

	// Subsystem 2: APS ADC
	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");

	sshsNodeCreateBool(apsNode, "Run", true, SSHS_FLAGS_NORMAL, "Enable APS (Frame events).");
	sshsNodeCreateBool(apsNode, "WaitOnTransferStall", true, SSHS_FLAGS_NORMAL,
		"On event FIFO full, pause and wait for free space. This ensures no APS pixels are dropped.");

	if (devInfo->apsHasGlobalShutter) {
		// Only support GS on chips that have it available.
		sshsNodeCreateBool(
			apsNode, "GlobalShutter", true, SSHS_FLAGS_NORMAL, "Enable global-shutter versus rolling-shutter mode.");
	}

	sshsNodeCreateInt(apsNode, "StartColumn0", 0, 0, I16T(devInfo->apsSizeX - 1), SSHS_FLAGS_NORMAL,
		"Column/X address of ROI 0 start point.");
	sshsNodeCreateInt(apsNode, "StartRow0", 0, 0, I16T(devInfo->apsSizeY - 1), SSHS_FLAGS_NORMAL,
		"Row/Y address of ROI 0 start point.");
	sshsNodeCreateInt(apsNode, "EndColumn0", I16T(devInfo->apsSizeX - 1), 0, I16T(devInfo->apsSizeX - 1),
		SSHS_FLAGS_NORMAL, "Column/X address of ROI 0 end point.");
	sshsNodeCreateInt(apsNode, "EndRow0", I16T(devInfo->apsSizeY - 1), 0, I16T(devInfo->apsSizeY - 1),
		SSHS_FLAGS_NORMAL, "Row/Y address of ROI 0 end point.");

	sshsNodeCreateInt(apsNode, "Exposure", 4000, 0, (0x01 << 22) - 1, SSHS_FLAGS_NORMAL, "Set exposure time (in µs).");
	// Initialize exposure in backend (libcaer), so that read-modifier that bypasses SSHS to access
	// libcaer directly will read the correct value.
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_EXPOSURE,
		U32T(sshsNodeGetInt(apsNode, "Exposure")));
	sshsAttributeUpdaterAdd(apsNode, "Exposure", SSHS_INT, &apsExposureUpdater, moduleData->moduleState);

	sshsNodeCreateInt(
		apsNode, "FrameInterval", 40000, 0, (0x01 << 23) - 1, SSHS_FLAGS_NORMAL, "Set time between frames (in µs).");

	sshsNodeCreateBool(apsNode, "TakeSnapShot", false, SSHS_FLAGS_NOTIFY_ONLY, "Take a single frame capture.");
	sshsNodeCreateBool(apsNode, "AutoExposure", true, SSHS_FLAGS_NORMAL,
		"Enable automatic exposure control, to react to changes in lighting conditions.");

	sshsNodeCreateString(apsNode, "FrameMode", "Default", 7, 9, SSHS_FLAGS_NORMAL,
		"Enable automatic exposure control, to react to changes in lighting conditions.");
	sshsNodeCreateAttributeListOptions(apsNode, "FrameMode", "Default,Grayscale,Original", false);

	// DAVIS RGB has additional timing counters.
	if (IS_DAVIS640H(devInfo->chipID)) {
		sshsNodeCreateInt(apsNode, "TransferTime", 1500, 0, (60 * 2048), SSHS_FLAGS_NORMAL,
			"Transfer time counter (2 in GS, 1 in RS, in cycles).");
		sshsNodeCreateInt(
			apsNode, "RSFDSettleTime", 1000, 0, (60 * 128), SSHS_FLAGS_NORMAL, "RS counter 0 (in cycles).");
		sshsNodeCreateInt(
			apsNode, "GSPDResetTime", 1000, 0, (60 * 128), SSHS_FLAGS_NORMAL, "GS counter 0 (in cycles).");
		sshsNodeCreateInt(
			apsNode, "GSResetFallTime", 1000, 0, (60 * 128), SSHS_FLAGS_NORMAL, "GS counter 1 (in cycles).");
		sshsNodeCreateInt(apsNode, "GSTXFallTime", 1000, 0, (60 * 128), SSHS_FLAGS_NORMAL, "GS counter 3 (in cycles).");
		sshsNodeCreateInt(
			apsNode, "GSFDResetTime", 1000, 0, (60 * 128), SSHS_FLAGS_NORMAL, "GS counter 4 (in cycles).");
	}

	// Subsystem 3: IMU
	if (devInfo->imuType != 0) {
		sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");

		sshsNodeCreateBool(imuNode, "RunAccel", true, SSHS_FLAGS_NORMAL, "Enable IMU accelerometer.");
		sshsNodeCreateBool(imuNode, "RunGyro", true, SSHS_FLAGS_NORMAL, "Enable IMU gyroscope.");
		sshsNodeCreateBool(imuNode, "RunTemp", true, SSHS_FLAGS_NORMAL, "Enable IMU temperature sensor.");
		sshsNodeCreateInt(imuNode, "SampleRateDivider", 0, 0, 255, SSHS_FLAGS_NORMAL, "Sample-rate divider value.");

		if (devInfo->imuType == 2) {
			// InvenSense MPU 9250 IMU.
			sshsNodeCreateInt(imuNode, "AccelDLPF", 1, 0, 7, SSHS_FLAGS_NORMAL,
				"Accelerometer digital low-pass filter configuration.");
			sshsNodeCreateInt(
				imuNode, "GyroDLPF", 1, 0, 7, SSHS_FLAGS_NORMAL, "Gyroscope digital low-pass filter configuration.");
		}
		else {
			// InvenSense MPU 6050/6150 IMU.
			sshsNodeCreateInt(imuNode, "DigitalLowPassFilter", 1, 0, 7, SSHS_FLAGS_NORMAL,
				"Accelerometer/Gyroscope digital low-pass filter configuration.");
		}

		sshsNodeCreateInt(imuNode, "AccelFullScale", 1, 0, 3, SSHS_FLAGS_NORMAL, "Accelerometer scale configuration.");
		sshsNodeCreateInt(imuNode, "GyroFullScale", 1, 0, 3, SSHS_FLAGS_NORMAL, "Gyroscope scale configuration.");
	}

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

		sshsNodeCreateLong(statNode, "muxDroppedExtInput", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped External Input events due to USB full.");
		sshsAttributeUpdaterAdd(statNode, "muxDroppedExtInput", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "muxDroppedDVS", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped DVS events due to USB full.");
		sshsAttributeUpdaterAdd(statNode, "muxDroppedDVS", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);
	}

	if (devInfo->dvsHasStatistics) {
		sshsNode statNode = sshsGetRelativeNode(deviceConfigNode, "statistics/");

		sshsNodeCreateLong(statNode, "dvsEventsRow", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of row events handled.");
		sshsAttributeUpdaterAdd(statNode, "dvsEventsRow", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "dvsEventsColumn", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of column events handled.");
		sshsAttributeUpdaterAdd(statNode, "dvsEventsColumn", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		sshsNodeCreateLong(statNode, "dvsEventsDropped", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
			"Number of dropped events (groups of events).");
		sshsAttributeUpdaterAdd(statNode, "dvsEventsDropped", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

		if (devInfo->dvsHasPixelFilter) {
			sshsNodeCreateLong(statNode, "dvsFilteredPixel", 0, 0, INT64_MAX,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Number of events filtered out by the Pixel Filter.");
			sshsAttributeUpdaterAdd(
				statNode, "dvsFilteredPixel", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);
		}

		if (devInfo->dvsHasBackgroundActivityFilter) {
			sshsNodeCreateLong(statNode, "dvsFilteredBA", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
				"Number of events filtered out by the Background Activity Filter.");
			sshsAttributeUpdaterAdd(statNode, "dvsFilteredBA", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);

			sshsNodeCreateLong(statNode, "dvsFilteredRefractory", 0, 0, INT64_MAX,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
				"Number of events filtered out by the Refractory Period Filter.");
			sshsAttributeUpdaterAdd(
				statNode, "dvsFilteredRefractory", SSHS_LONG, &statisticsUpdater, moduleData->moduleState);
		}
	}
}

static void biasConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// All chips of a kind have the same bias address for the same bias!
	if (IS_DAVIS240(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFBN,
			generateCoarseFineBiasParent(node, "DiffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_ONBN,
			generateCoarseFineBiasParent(node, "OnBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_OFFBN,
			generateCoarseFineBiasParent(node, "OffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSCASEPC,
			generateCoarseFineBiasParent(node, "ApsCasEpc"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFCASBNC,
			generateCoarseFineBiasParent(node, "DiffCasBnc"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSROSFBN,
			generateCoarseFineBiasParent(node, "ApsROSFBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LOCALBUFBN,
			generateCoarseFineBiasParent(node, "LocalBufBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PIXINVBN,
			generateCoarseFineBiasParent(node, "PixInvBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRBP,
			generateCoarseFineBiasParent(node, "PrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRSFBP,
			generateCoarseFineBiasParent(node, "PrSFBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_REFRBP,
			generateCoarseFineBiasParent(node, "RefrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPDBN,
			generateCoarseFineBiasParent(node, "AEPdBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LCOLTIMEOUTBN,
			generateCoarseFineBiasParent(node, "LcolTimeoutBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUXBP,
			generateCoarseFineBiasParent(node, "AEPuXBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUYBP,
			generateCoarseFineBiasParent(node, "AEPuYBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFTHRBN,
			generateCoarseFineBiasParent(node, "IFThrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFREFRBN,
			generateCoarseFineBiasParent(node, "IFRefrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PADFOLLBN,
			generateCoarseFineBiasParent(node, "PadFollBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSOVERFLOWLEVELBN,
			generateCoarseFineBiasParent(node, "ApsOverflowLevelBn"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_BIASBUFFER,
			generateCoarseFineBiasParent(node, "BiasBuffer"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSP,
			generateShiftedSourceBiasParent(node, "SSP"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSN,
			generateShiftedSourceBiasParent(node, "SSN"));
	}

	if (IS_DAVIS128(devInfo->chipID) || IS_DAVIS208(devInfo->chipID) || IS_DAVIS346(devInfo->chipID)
		|| IS_DAVIS640(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSOVERFLOWLEVEL,
			generateVDACBiasParent(node, "ApsOverflowLevel"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSCAS,
			generateVDACBiasParent(node, "ApsCas"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFHIGH,
			generateVDACBiasParent(node, "AdcRefHigh"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFLOW,
			generateVDACBiasParent(node, "AdcRefLow"));

		if (IS_DAVIS346(devInfo->chipID) || IS_DAVIS640(devInfo->chipID)) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS346_CONFIG_BIAS_ADCTESTVOLTAGE,
				generateVDACBiasParent(node, "AdcTestVoltage"));
		}

		if (IS_DAVIS208(devInfo->chipID)) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_RESETHIGHPASS,
				generateVDACBiasParent(node, "ResetHighPass"));
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSS,
				generateVDACBiasParent(node, "RefSS"));

			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REGBIASBP,
				generateCoarseFineBiasParent(node, "RegBiasBp"));
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSSBN,
				generateCoarseFineBiasParent(node, "RefSSBn"));
		}

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LOCALBUFBN,
			generateCoarseFineBiasParent(node, "LocalBufBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PADFOLLBN,
			generateCoarseFineBiasParent(node, "PadFollBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DIFFBN,
			generateCoarseFineBiasParent(node, "DiffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ONBN,
			generateCoarseFineBiasParent(node, "OnBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_OFFBN,
			generateCoarseFineBiasParent(node, "OffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PIXINVBN,
			generateCoarseFineBiasParent(node, "PixInvBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRBP,
			generateCoarseFineBiasParent(node, "PrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRSFBP,
			generateCoarseFineBiasParent(node, "PrSFBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_REFRBP,
			generateCoarseFineBiasParent(node, "RefrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_READOUTBUFBP,
			generateCoarseFineBiasParent(node, "ReadoutBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSROSFBN,
			generateCoarseFineBiasParent(node, "ApsROSFBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCCOMPBP,
			generateCoarseFineBiasParent(node, "AdcCompBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_COLSELLOWBN,
			generateCoarseFineBiasParent(node, "ColSelLowBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DACBUFBP,
			generateCoarseFineBiasParent(node, "DACBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LCOLTIMEOUTBN,
			generateCoarseFineBiasParent(node, "LcolTimeoutBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPDBN,
			generateCoarseFineBiasParent(node, "AEPdBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUXBP,
			generateCoarseFineBiasParent(node, "AEPuXBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUYBP,
			generateCoarseFineBiasParent(node, "AEPuYBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFREFRBN,
			generateCoarseFineBiasParent(node, "IFRefrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFTHRBN,
			generateCoarseFineBiasParent(node, "IFThrBn"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_BIASBUFFER,
			generateCoarseFineBiasParent(node, "BiasBuffer"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSP,
			generateShiftedSourceBiasParent(node, "SSP"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSN,
			generateShiftedSourceBiasParent(node, "SSN"));
	}

	if (IS_DAVIS640H(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_APSCAS,
			generateVDACBiasParent(node, "ApsCas"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_OVG1LO,
			generateVDACBiasParent(node, "OVG1Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_OVG2LO,
			generateVDACBiasParent(node, "OVG2Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_TX2OVG2HI,
			generateVDACBiasParent(node, "TX2OVG2Hi"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_GND07,
			generateVDACBiasParent(node, "Gnd07"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCTESTVOLTAGE,
			generateVDACBiasParent(node, "AdcTestVoltage"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCREFHIGH,
			generateVDACBiasParent(node, "AdcRefHigh"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCREFLOW,
			generateVDACBiasParent(node, "AdcRefLow"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_IFREFRBN,
			generateCoarseFineBiasParent(node, "IFRefrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_IFTHRBN,
			generateCoarseFineBiasParent(node, "IFThrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_LOCALBUFBN,
			generateCoarseFineBiasParent(node, "LocalBufBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PADFOLLBN,
			generateCoarseFineBiasParent(node, "PadFollBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PIXINVBN,
			generateCoarseFineBiasParent(node, "PixInvBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_DIFFBN,
			generateCoarseFineBiasParent(node, "DiffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ONBN,
			generateCoarseFineBiasParent(node, "OnBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_OFFBN,
			generateCoarseFineBiasParent(node, "OffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PRBP,
			generateCoarseFineBiasParent(node, "PrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PRSFBP,
			generateCoarseFineBiasParent(node, "PrSFBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_REFRBP,
			generateCoarseFineBiasParent(node, "RefrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ARRAYBIASBUFFERBN,
			generateCoarseFineBiasParent(node, "ArrayBiasBufferBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ARRAYLOGICBUFFERBN,
			generateCoarseFineBiasParent(node, "ArrayLogicBufferBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_FALLTIMEBN,
			generateCoarseFineBiasParent(node, "FalltimeBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_RISETIMEBP,
			generateCoarseFineBiasParent(node, "RisetimeBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_READOUTBUFBP,
			generateCoarseFineBiasParent(node, "ReadoutBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_APSROSFBN,
			generateCoarseFineBiasParent(node, "ApsROSFBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCCOMPBP,
			generateCoarseFineBiasParent(node, "AdcCompBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_DACBUFBP,
			generateCoarseFineBiasParent(node, "DACBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_LCOLTIMEOUTBN,
			generateCoarseFineBiasParent(node, "LcolTimeoutBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_AEPDBN,
			generateCoarseFineBiasParent(node, "AEPdBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_AEPUXBP,
			generateCoarseFineBiasParent(node, "AEPuXBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_AEPUYBP,
			generateCoarseFineBiasParent(node, "AEPuYBp"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_BIASBUFFER,
			generateCoarseFineBiasParent(node, "BiasBuffer"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_SSP,
			generateShiftedSourceBiasParent(node, "SSP"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_SSN,
			generateShiftedSourceBiasParent(node, "SSN"));
	}
}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData moduleData      = userData;
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		const char *nodeName = sshsNodeGetName(node);

		if (IS_DAVIS240(devInfo.chipID)) {
			if (caerStrEquals(nodeName, "DiffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OnBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_ONBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_OFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsCasEpc")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSCASEPC,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DiffCasBnc")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFCASBNC,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsROSFBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSROSFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LocalBufBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LOCALBUFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PixInvBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PIXINVBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrSFBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRSFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RefrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_REFRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPdBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPDBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LcolTimeoutBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LCOLTIMEOUTBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuXBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUXBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuYBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUYBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFThrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFTHRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFRefrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFREFRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PadFollBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PADFOLLBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsOverflowLevelBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSOVERFLOWLEVELBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "BiasBuffer")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_BIASBUFFER,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSP,
					generateShiftedSourceBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSN,
					generateShiftedSourceBias(node));
			}
		}

		if (IS_DAVIS128(devInfo.chipID) || IS_DAVIS208(devInfo.chipID) || IS_DAVIS346(devInfo.chipID)
			|| IS_DAVIS640(devInfo.chipID)) {
			if (caerStrEquals(nodeName, "ApsOverflowLevel")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSOVERFLOWLEVEL,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsCas")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSCAS, generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefHigh")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFHIGH,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefLow")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFLOW, generateVDACBias(node));
			}
			else if ((IS_DAVIS346(devInfo.chipID) || IS_DAVIS640(devInfo.chipID))
					 && caerStrEquals(nodeName, "AdcTestVoltage")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS346_CONFIG_BIAS_ADCTESTVOLTAGE,
					generateVDACBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "ResetHighPass")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_RESETHIGHPASS,
					generateVDACBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "RefSS")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSS, generateVDACBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "RegBiasBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REGBIASBP,
					generateCoarseFineBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "RefSSBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSSBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LocalBufBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LOCALBUFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PadFollBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PADFOLLBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DiffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DIFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OnBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ONBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_OFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PixInvBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PIXINVBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrSFBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRSFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RefrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_REFRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ReadoutBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_READOUTBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsROSFBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSROSFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcCompBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCCOMPBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ColSelLowBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_COLSELLOWBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DACBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DACBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LcolTimeoutBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LCOLTIMEOUTBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPdBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPDBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuXBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUXBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuYBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUYBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFRefrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFREFRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFThrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFTHRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "BiasBuffer")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_BIASBUFFER,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSP,
					generateShiftedSourceBias(node));
			}
			else if (caerStrEquals(nodeName, "SSN")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSN,
					generateShiftedSourceBias(node));
			}
		}

		if (IS_DAVIS640H(devInfo.chipID)) {
			if (caerStrEquals(nodeName, "ApsCas")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_APSCAS, generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "OVG1Lo")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_OVG1LO, generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "OVG2Lo")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_OVG2LO, generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "TX2OVG2Hi")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_TX2OVG2HI,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "Gnd07")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_GND07, generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcTestVoltage")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCTESTVOLTAGE,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefHigh")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCREFHIGH,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefLow")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCREFLOW,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "IFRefrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_IFREFRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFThrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_IFTHRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LocalBufBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_LOCALBUFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PadFollBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PADFOLLBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PixInvBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PIXINVBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DiffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_DIFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OnBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ONBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_OFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrSFBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_PRSFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RefrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_REFRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ArrayBiasBufferBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ARRAYBIASBUFFERBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ArrayLogicBufferBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS,
					DAVIS640H_CONFIG_BIAS_ARRAYLOGICBUFFERBN, generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "FalltimeBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_FALLTIMEBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RisetimeBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_RISETIMEBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ReadoutBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_READOUTBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsROSFBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_APSROSFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcCompBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_ADCCOMPBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DACBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_DACBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LcolTimeoutBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_LCOLTIMEOUTBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPdBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_AEPDBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuXBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_AEPUXBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuYBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_AEPUYBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "BiasBuffer")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_BIASBUFFER,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_SSP,
					generateShiftedSourceBias(node));
			}
			else if (caerStrEquals(nodeName, "SSN")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS640H_CONFIG_BIAS_SSN,
					generateShiftedSourceBias(node));
			}
		}
	}
}

static void chipConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// All chips have the same parameter address for the same setting!
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX0,
		U32T(sshsNodeGetInt(node, "DigitalMux0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX1,
		U32T(sshsNodeGetInt(node, "DigitalMux1")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX2,
		U32T(sshsNodeGetInt(node, "DigitalMux2")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX3,
		U32T(sshsNodeGetInt(node, "DigitalMux3")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX0,
		U32T(sshsNodeGetInt(node, "AnalogMux0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX1,
		U32T(sshsNodeGetInt(node, "AnalogMux1")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX2,
		U32T(sshsNodeGetInt(node, "AnalogMux2")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_BIASMUX0,
		U32T(sshsNodeGetInt(node, "BiasMux0")));

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETCALIBNEURON,
		sshsNodeGetBool(node, "ResetCalibNeuron"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_TYPENCALIBNEURON,
		sshsNodeGetBool(node, "TypeNCalibNeuron"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETTESTPIXEL,
		sshsNodeGetBool(node, "ResetTestPixel"));
	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_AERNAROW, sshsNodeGetBool(node, "AERnArow"));
	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_USEAOUT, sshsNodeGetBool(node, "UseAOut"));

	if (IS_DAVIS240A(devInfo->chipID) || IS_DAVIS240B(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS240_CONFIG_CHIP_SPECIALPIXELCONTROL,
			sshsNodeGetBool(node, "SpecialPixelControl"));
	}

	if (IS_DAVIS128(devInfo->chipID) || IS_DAVIS208(devInfo->chipID) || IS_DAVIS346(devInfo->chipID)
		|| IS_DAVIS640(devInfo->chipID) || IS_DAVIS640H(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_SELECTGRAYCOUNTER,
			sshsNodeGetBool(node, "SelectGrayCounter"));
	}

	if (IS_DAVIS346(devInfo->chipID) || IS_DAVIS640(devInfo->chipID) || IS_DAVIS640H(devInfo->chipID)) {
		caerDeviceConfigSet(
			moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS346_CONFIG_CHIP_TESTADC, sshsNodeGetBool(node, "TestADC"));
	}

	if (IS_DAVIS208(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPREAMPAVG,
			sshsNodeGetBool(node, "SelectPreAmpAvg"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTBIASREFSS,
			sshsNodeGetBool(node, "SelectBiasRefSS"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTSENSE,
			sshsNodeGetBool(node, "SelectSense"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPOSFB,
			sshsNodeGetBool(node, "SelectPosFb"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTHIGHPASS,
			sshsNodeGetBool(node, "SelectHighPass"));
	}

	if (IS_DAVIS640H(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS640H_CONFIG_CHIP_ADJUSTOVG1LO,
			sshsNodeGetBool(node, "AdjustOVG1Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS640H_CONFIG_CHIP_ADJUSTOVG2LO,
			sshsNodeGetBool(node, "AdjustOVG2Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS640H_CONFIG_CHIP_ADJUSTTX2OVG2HI,
			sshsNodeGetBool(node, "AdjustTX2OVG2Hi"));
	}
}

static void chipConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData      = userData;
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "DigitalMux0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DigitalMux1")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX1, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DigitalMux2")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX2, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DigitalMux3")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX3, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AnalogMux0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AnalogMux1")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX1, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AnalogMux2")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX2, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BiasMux0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_BIASMUX0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ResetCalibNeuron")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETCALIBNEURON, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TypeNCalibNeuron")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_TYPENCALIBNEURON, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ResetTestPixel")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETTESTPIXEL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AERnArow")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_AERNAROW, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "UseAOut")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_USEAOUT, changeValue.boolean);
		}
		else if ((IS_DAVIS240A(devInfo.chipID) || IS_DAVIS240B(devInfo.chipID)) && changeType == SSHS_BOOL
				 && caerStrEquals(changeKey, "SpecialPixelControl")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS240_CONFIG_CHIP_SPECIALPIXELCONTROL,
				changeValue.boolean);
		}
		else if ((IS_DAVIS128(devInfo.chipID) || IS_DAVIS208(devInfo.chipID) || IS_DAVIS346(devInfo.chipID)
					 || IS_DAVIS640(devInfo.chipID) || IS_DAVIS640H(devInfo.chipID))
				 && changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectGrayCounter")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_SELECTGRAYCOUNTER,
				changeValue.boolean);
		}
		else if ((IS_DAVIS346(devInfo.chipID) || IS_DAVIS640(devInfo.chipID) || IS_DAVIS640H(devInfo.chipID))
				 && changeType == SSHS_BOOL && caerStrEquals(changeKey, "TestADC")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS346_CONFIG_CHIP_TESTADC, changeValue.boolean);
		}

		if (IS_DAVIS208(devInfo.chipID)) {
			if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectPreAmpAvg")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPREAMPAVG,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectBiasRefSS")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTBIASREFSS,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectSense")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTSENSE, changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectPosFb")) {
				caerDeviceConfigSet(
					moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPOSFB, changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectHighPass")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTHIGHPASS,
					changeValue.boolean);
			}
		}

		if (IS_DAVIS640H(devInfo.chipID)) {
			if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AdjustOVG1Lo")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS640H_CONFIG_CHIP_ADJUSTOVG1LO,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AdjustOVG2Lo")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS640H_CONFIG_CHIP_ADJUSTOVG2LO,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AdjustTX2OVG2Hi")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS640H_CONFIG_CHIP_ADJUSTTX2OVG2HI,
					changeValue.boolean);
			}
		}
	}
}

static void muxConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RESET,
		sshsNodeGetBool(node, "TimestampReset"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_EXTINPUT_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropExtInputOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_DVS_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropDVSOnTransferStall"));
	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN_CHIP, sshsNodeGetBool(node, "RunChip"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RUN,
		sshsNodeGetBool(node, "TimestampRun"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN, sshsNodeGetBool(node, "Run"));
}

static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampReset")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RESET, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropExtInputOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX,
				DAVIS_CONFIG_MUX_DROP_EXTINPUT_ON_TRANSFER_STALL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropDVSOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_DVS_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunChip")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN_CHIP, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampRun")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RUN, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN, changeValue.boolean);
		}
	}
}

static void dvsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_WAIT_ON_TRANSFER_STALL,
		U32T(sshsNodeGetBool(node, "WaitOnTransferStall")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_EXTERNAL_AER_CONTROL,
		U32T(sshsNodeGetBool(node, "ExternalAERControl")));

	if (devInfo->dvsHasPixelFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel0Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel0Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel1Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel1Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel2Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel2Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel3Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel3Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel4Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel4Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel5Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel5Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel6Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel6Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_ROW,
			U32T(sshsNodeGetInt(node, "FilterPixel7Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterPixel7Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_AUTO_TRAIN,
			sshsNodeGetBool(node, "FilterPixelAutoTrain"));
	}

	if (devInfo->dvsHasBackgroundActivityFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY,
			sshsNodeGetBool(node, "FilterBackgroundActivity"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY_TIME,
			U32T(sshsNodeGetInt(node, "FilterBackgroundActivityTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_REFRACTORY_PERIOD,
			sshsNodeGetBool(node, "FilterRefractoryPeriod"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_REFRACTORY_PERIOD_TIME,
			U32T(sshsNodeGetInt(node, "FilterRefractoryPeriodTime")));
	}

	if (devInfo->dvsHasROIFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_START_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterROIStartColumn")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_START_ROW,
			U32T(sshsNodeGetInt(node, "FilterROIStartRow")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_END_COLUMN,
			U32T(sshsNodeGetInt(node, "FilterROIEndColumn")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_END_ROW,
			U32T(sshsNodeGetInt(node, "FilterROIEndRow")));
	}

	if (devInfo->dvsHasSkipFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_SKIP_EVENTS,
			sshsNodeGetBool(node, "FilterSkipEvents"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_SKIP_EVENTS_EVERY,
			U32T(sshsNodeGetInt(node, "FilterSkipEventsEvery")));
	}

	if (devInfo->dvsHasPolarityFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_POLARITY_FLATTEN,
			sshsNodeGetBool(node, "FilterPolarityFlatten"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_POLARITY_SUPPRESS,
			sshsNodeGetBool(node, "FilterPolaritySuppress"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_POLARITY_SUPPRESS_TYPE,
			sshsNodeGetBool(node, "FilterPolaritySuppressType"));
	}

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_RUN, sshsNodeGetBool(node, "Run"));
}

static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "WaitOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_WAIT_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ExternalAERControl")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_EXTERNAL_AER_CONTROL, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel0Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel0Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel1Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel1Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel2Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel2Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel3Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel3Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel4Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel4Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel5Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel5Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel6Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel6Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel7Row")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterPixel7Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterPixelAutoTrain")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_AUTO_TRAIN,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterBackgroundActivity")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY,
				changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterBackgroundActivityTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS,
				DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY_TIME, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterRefractoryPeriod")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_REFRACTORY_PERIOD,
				changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterRefractoryPeriodTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS,
				DAVIS_CONFIG_DVS_FILTER_REFRACTORY_PERIOD_TIME, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterROIStartColumn")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_START_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterROIStartRow")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_START_ROW,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterROIEndColumn")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_END_COLUMN,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterROIEndRow")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROI_END_ROW, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterSkipEvents")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_SKIP_EVENTS, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterSkipEventsEvery")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_SKIP_EVENTS_EVERY,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterPolarityFlatten")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_POLARITY_FLATTEN,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterPolaritySuppress")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_POLARITY_SUPPRESS,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterPolaritySuppressType")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS,
				DAVIS_CONFIG_DVS_FILTER_POLARITY_SUPPRESS_TYPE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_RUN, changeValue.boolean);
		}
	}
}

static inline uint32_t parseAPSFrameMode(char *configStr) {
	if (caerStrEquals(configStr, "Default")) {
		return (APS_FRAME_DEFAULT);
	}
	else if (caerStrEquals(configStr, "Grayscale")) {
		return (APS_FRAME_GRAYSCALE);
	}
	else {
		return (APS_FRAME_ORIGINAL);
	}
}

static void apsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_WAIT_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "WaitOnTransferStall"));

	if (devInfo->apsHasGlobalShutter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_GLOBAL_SHUTTER,
			sshsNodeGetBool(node, "GlobalShutter"));
	}

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_0,
		U32T(sshsNodeGetInt(node, "StartColumn0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_0,
		U32T(sshsNodeGetInt(node, "StartRow0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_0,
		U32T(sshsNodeGetInt(node, "EndColumn0")));
	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_0, U32T(sshsNodeGetInt(node, "EndRow0")));

	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_EXPOSURE, U32T(sshsNodeGetInt(node, "Exposure")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_FRAME_INTERVAL,
		U32T(sshsNodeGetInt(node, "FrameInterval")));

	// DAVIS RGB extra timing support.
	if (IS_DAVIS640H(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_TRANSFER,
			U32T(sshsNodeGetInt(node, "TransferTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_RSFDSETTLE,
			U32T(sshsNodeGetInt(node, "RSFDSettleTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSPDRESET,
			U32T(sshsNodeGetInt(node, "GSPDResetTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSRESETFALL,
			U32T(sshsNodeGetInt(node, "GSResetFallTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSTXFALL,
			U32T(sshsNodeGetInt(node, "GSTXFallTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSFDRESET,
			U32T(sshsNodeGetInt(node, "GSFDResetTime")));
	}

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_AUTOEXPOSURE,
		sshsNodeGetBool(node, "AutoExposure"));

	char *frameModeStr = sshsNodeGetString(node, "FrameMode");
	caerDeviceConfigSet(
		moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_FRAME_MODE, parseAPSFrameMode(frameModeStr));
	free(frameModeStr);

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, sshsNodeGetBool(node, "Run"));
}

static void apsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "WaitOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_WAIT_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GlobalShutter")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_GLOBAL_SHUTTER, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "StartColumn0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "StartRow0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "EndColumn0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "EndRow0")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_0, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "Exposure")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_EXPOSURE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FrameInterval")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_FRAME_INTERVAL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "TransferTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_TRANSFER, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "RSFDSettleTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_RSFDSETTLE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GSPDResetTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSPDRESET, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GSResetFallTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSRESETFALL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GSTXFallTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSTXFALL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GSFDResetTime")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS640H_CONFIG_APS_GSFDRESET, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TakeSnapShot")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SNAPSHOT, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AutoExposure")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_AUTOEXPOSURE, changeValue.boolean);
		}
		else if (changeType == SSHS_STRING && caerStrEquals(changeKey, "FrameMode")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_FRAME_MODE,
				parseAPSFrameMode(changeValue.string));
		}
	}
}

static void imuConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	if (devInfo->imuType != 0) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_SAMPLE_RATE_DIVIDER,
			U32T(sshsNodeGetInt(node, "SampleRateDivider")));

		if (devInfo->imuType == 2) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_DLPF,
				U32T(sshsNodeGetInt(node, "AccelDLPF")));
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_DLPF,
				U32T(sshsNodeGetInt(node, "GyroDLPF")));
		}
		else {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_DIGITAL_LOW_PASS_FILTER,
				U32T(sshsNodeGetInt(node, "DigitalLowPassFilter")));
		}

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_FULL_SCALE,
			U32T(sshsNodeGetInt(node, "AccelFullScale")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_FULL_SCALE,
			U32T(sshsNodeGetInt(node, "GyroFullScale")));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_ACCELEROMETER,
			sshsNodeGetBool(node, "RunAccel"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_GYROSCOPE,
			sshsNodeGetBool(node, "RunGyro"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_TEMPERATURE,
			sshsNodeGetBool(node, "RunTemp"));
	}
}

static void imuConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "SampleRateDivider")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_SAMPLE_RATE_DIVIDER,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DigitalLowPassFilter")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_DIGITAL_LOW_PASS_FILTER,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AccelDLPF")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_DLPF, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "AccelFullScale")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_FULL_SCALE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GyroDLPF")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_DLPF, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GyroFullScale")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_FULL_SCALE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunAccel")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_ACCELEROMETER, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunGyro")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_GYROSCOPE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunTemp")) {
			caerDeviceConfigSet(
				moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_TEMPERATURE, changeValue.boolean);
		}
	}
}

static void extInputConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES,
		sshsNodeGetBool(node, "DetectRisingEdges"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES,
		sshsNodeGetBool(node, "DetectFallingEdges"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES,
		sshsNodeGetBool(node, "DetectPulses"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY,
		sshsNodeGetBool(node, "DetectPulsePolarity"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH,
		U32T(sshsNodeGetInt(node, "DetectPulseLength")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR,
		sshsNodeGetBool(node, "RunDetector"));

	if (devInfo->extInputHasGenerator) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_POLARITY, sshsNodeGetBool(node, "GeneratePulsePolarity"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_INTERVAL, U32T(sshsNodeGetInt(node, "GeneratePulseInterval")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_LENGTH,
			U32T(sshsNodeGetInt(node, "GeneratePulseLength")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_RISING_EDGE, sshsNodeGetBool(node, "GenerateInjectOnRisingEdge"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_FALLING_EDGE,
			sshsNodeGetBool(node, "GenerateInjectOnFallingEdge"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_GENERATOR,
			sshsNodeGetBool(node, "RunGenerator"));
	}
}

static void extInputConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectRisingEdges")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectFallingEdges")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulses")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulsePolarity")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DetectPulseLength")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunDetector")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GeneratePulsePolarity")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_POLARITY, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GeneratePulseInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_INTERVAL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GeneratePulseLength")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_LENGTH, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateInjectOnRisingEdge")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_RISING_EDGE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateInjectOnFallingEdge")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
				DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_FALLING_EDGE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunGenerator")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_GENERATOR,
				changeValue.boolean);
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

	caerDeviceHandle handle                   = userData;
	union sshs_node_attr_value statisticValue = {.ilong = 0};

	if (caerStrEquals(key, "muxDroppedExtInput")) {
		caerDeviceConfigGet64(
			handle, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_STATISTICS_EXTINPUT_DROPPED, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "muxDroppedDVS")) {
		caerDeviceConfigGet64(
			handle, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_STATISTICS_DVS_DROPPED, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsEventsRow")) {
		caerDeviceConfigGet64(
			handle, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_STATISTICS_EVENTS_ROW, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsEventsColumn")) {
		caerDeviceConfigGet64(
			handle, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_STATISTICS_EVENTS_COLUMN, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsEventsDropped")) {
		caerDeviceConfigGet64(
			handle, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_STATISTICS_EVENTS_DROPPED, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsFilteredPixel")) {
		caerDeviceConfigGet64(
			handle, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_STATISTICS_FILTERED_PIXELS, (uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsFilteredBA")) {
		caerDeviceConfigGet64(handle, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_STATISTICS_FILTERED_BACKGROUND_ACTIVITY,
			(uint64_t *) &statisticValue.ilong);
	}
	else if (caerStrEquals(key, "dvsFilteredRefractory")) {
		caerDeviceConfigGet64(handle, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_STATISTICS_FILTERED_REFRACTORY_PERIOD,
			(uint64_t *) &statisticValue.ilong);
	}

	return (statisticValue);
}

static union sshs_node_attr_value apsExposureUpdater(
	void *userData, const char *key, enum sshs_node_attr_value_type type) {
	UNUSED_ARGUMENT(key);  // This is for the Exposure key only.
	UNUSED_ARGUMENT(type); // We know Exposure is always INT.

	caerDeviceHandle handle                         = userData;
	union sshs_node_attr_value currentExposureValue = {.iint = 0};

	caerDeviceConfigGet(handle, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_EXPOSURE, (uint32_t *) &currentExposureValue.iint);

	return (currentExposureValue);
}

static void createVDACBiasSetting(sshsNode biasNode, const char *biasName, uint8_t voltageValue, uint8_t currentValue) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength]     = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateInt(biasConfigNode, "voltageValue", I8T(voltageValue), 0, 63, SSHS_FLAGS_NORMAL,
		"Voltage, as a fraction of 1/64th of VDD=3.3V.");
	sshsNodeCreateInt(
		biasConfigNode, "currentValue", I8T(currentValue), 0, 7, SSHS_FLAGS_NORMAL, "Current that drives the voltage.");
}

static uint16_t generateVDACBiasParent(sshsNode biasNode, const char *biasName) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength]     = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get bias configuration node.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	return (generateVDACBias(biasConfigNode));
}

static uint16_t generateVDACBias(sshsNode biasNode) {
	// Build up bias value from all its components.
	struct caer_bias_vdac biasValue = {
		.voltageValue = U8T(sshsNodeGetInt(biasNode, "voltageValue")),
		.currentValue = U8T(sshsNodeGetInt(biasNode, "currentValue")),
	};

	return (caerBiasVDACGenerate(biasValue));
}

static void createCoarseFineBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
	bool enabled, const char *sex, const char *type) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength]     = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateInt(biasConfigNode, "coarseValue", I8T(coarseValue), 0, 7, SSHS_FLAGS_NORMAL,
		"Coarse current value (big adjustments).");
	sshsNodeCreateInt(biasConfigNode, "fineValue", I16T(fineValue), 0, 255, SSHS_FLAGS_NORMAL,
		"Fine current value (small adjustments).");
	sshsNodeCreateBool(biasConfigNode, "enabled", enabled, SSHS_FLAGS_NORMAL, "Bias enabled.");
	sshsNodeCreateString(biasConfigNode, "sex", sex, 1, 1, SSHS_FLAGS_NORMAL, "Bias sex.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "sex", "N,P", false);
	sshsNodeCreateString(biasConfigNode, "type", type, 6, 7, SSHS_FLAGS_NORMAL, "Bias type.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "type", "Normal,Cascode", false);
	sshsNodeCreateString(biasConfigNode, "currentLevel", "Normal", 3, 6, SSHS_FLAGS_NORMAL, "Bias current level.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "currentLevel", "Normal,Low", false);
}

static uint16_t generateCoarseFineBiasParent(sshsNode biasNode, const char *biasName) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength]     = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get bias configuration node.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	return (generateCoarseFineBias(biasConfigNode));
}

static uint16_t generateCoarseFineBias(sshsNode biasNode) {
	// Build up bias value from all its components.
	char *sexString          = sshsNodeGetString(biasNode, "sex");
	char *typeString         = sshsNodeGetString(biasNode, "type");
	char *currentLevelString = sshsNodeGetString(biasNode, "currentLevel");

	struct caer_bias_coarsefine biasValue = {
		.coarseValue        = U8T(sshsNodeGetInt(biasNode, "coarseValue")),
		.fineValue          = U8T(sshsNodeGetInt(biasNode, "fineValue")),
		.enabled            = sshsNodeGetBool(biasNode, "enabled"),
		.sexN               = caerStrEquals(sexString, "N"),
		.typeNormal         = caerStrEquals(typeString, "Normal"),
		.currentLevelNormal = caerStrEquals(currentLevelString, "Normal"),
	};

	// Free strings to avoid memory leaks.
	free(sexString);
	free(typeString);
	free(currentLevelString);

	return (caerBiasCoarseFineGenerate(biasValue));
}

static void createShiftedSourceBiasSetting(sshsNode biasNode, const char *biasName, uint8_t refValue, uint8_t regValue,
	const char *operatingMode, const char *voltageLevel) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength]     = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateInt(
		biasConfigNode, "refValue", I8T(refValue), 0, 63, SSHS_FLAGS_NORMAL, "Shifted-source bias level.");
	sshsNodeCreateInt(biasConfigNode, "regValue", I8T(regValue), 0, 63, SSHS_FLAGS_NORMAL,
		"Shifted-source bias current for buffer amplifier.");
	sshsNodeCreateString(
		biasConfigNode, "operatingMode", operatingMode, 3, 13, SSHS_FLAGS_NORMAL, "Shifted-source operating mode.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "operatingMode", "ShiftedSource,HiZ,TiedToRail", false);
	sshsNodeCreateString(
		biasConfigNode, "voltageLevel", voltageLevel, 9, 11, SSHS_FLAGS_NORMAL, "Shifted-source voltage level.");
	sshsNodeCreateAttributeListOptions(biasConfigNode, "voltageLevel", "SplitGate,SingleDiode,DoubleDiode", false);
}

static uint16_t generateShiftedSourceBiasParent(sshsNode biasNode, const char *biasName) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength]     = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get bias configuration node.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	return (generateShiftedSourceBias(biasConfigNode));
}

static uint16_t generateShiftedSourceBias(sshsNode biasNode) {
	// Build up bias value from all its components.
	char *operatingModeString = sshsNodeGetString(biasNode, "operatingMode");
	char *voltageLevelString  = sshsNodeGetString(biasNode, "voltageLevel");

	struct caer_bias_shiftedsource biasValue = {
		.refValue      = U8T(sshsNodeGetInt(biasNode, "refValue")),
		.regValue      = U8T(sshsNodeGetInt(biasNode, "regValue")),
		.operatingMode = (caerStrEquals(operatingModeString, "HiZ"))
							 ? (HI_Z)
							 : ((caerStrEquals(operatingModeString, "TiedToRail")) ? (TIED_TO_RAIL) : (SHIFTED_SOURCE)),
		.voltageLevel = (caerStrEquals(voltageLevelString, "SingleDiode"))
							? (SINGLE_DIODE)
							: ((caerStrEquals(voltageLevelString, "DoubleDiode")) ? (DOUBLE_DIODE) : (SPLIT_GATE)),
	};

	// Free strings to avoid memory leaks.
	free(operatingModeString);
	free(voltageLevelString);

	return (caerBiasShiftedSourceGenerate(biasValue));
}

#endif /* MODULES_INI_DAVIS_UTILS_H_ */
