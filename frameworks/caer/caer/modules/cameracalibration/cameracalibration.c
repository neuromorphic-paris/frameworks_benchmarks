#include <libcaer/events/frame.h>
#include <libcaer/events/polarity.h>

#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/mainloop.h"

#include "calibration_settings.h"
#include "calibration_wrapper.h"

struct CameraCalibrationState_struct {
	struct CameraCalibrationSettings_struct settings; // Struct containing all settings (shared)
	struct Calibration *cpp_class;                    // Pointer to cpp_class_object
	uint64_t lastFrameTimestamp;
	size_t lastFoundPoints;
	bool calibrationCompleted;
	bool calibrationLoaded;
};

typedef struct CameraCalibrationState_struct *CameraCalibrationState;

static void caerCameraCalibrationConfigInit(sshsNode moduleNode);
static bool caerCameraCalibrationInit(caerModuleData moduleData);
static void caerCameraCalibrationRun(
	caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerCameraCalibrationConfig(caerModuleData moduleData);
static void caerCameraCalibrationExit(caerModuleData moduleData);
static void updateSettings(caerModuleData moduleData);

static const struct caer_module_functions CameraCalibrationFunctions
	= {.moduleConfigInit = &caerCameraCalibrationConfigInit,
		.moduleInit      = &caerCameraCalibrationInit,
		.moduleRun       = &caerCameraCalibrationRun,
		.moduleConfig    = &caerCameraCalibrationConfig,
		.moduleExit      = &caerCameraCalibrationExit,
		.moduleReset     = NULL};

static const struct caer_event_stream_in CameraCalibrationInputs[]
	= {{.type = POLARITY_EVENT, .number = 1, .readOnly = false}, {.type = FRAME_EVENT, .number = 1, .readOnly = false}};

static const struct caer_module_info CameraCalibrationInfo = {
	.version           = 1,
	.name              = "CameraCalibration",
	.description       = "Lens distortion calibration, for undistortion of both events and frames.",
	.type              = CAER_MODULE_PROCESSOR,
	.memSize           = sizeof(struct CameraCalibrationState_struct),
	.functions         = &CameraCalibrationFunctions,
	.inputStreams      = CameraCalibrationInputs,
	.inputStreamsSize  = CAER_EVENT_STREAM_IN_SIZE(CameraCalibrationInputs),
	.outputStreams     = NULL,
	.outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&CameraCalibrationInfo);
}

static void caerCameraCalibrationConfigInit(sshsNode moduleNode) {
	sshsNodeCreateBool(moduleNode, "doCalibration", false, SSHS_FLAGS_NORMAL, "Do calibration using live images.");
	sshsNodeCreateString(moduleNode, "saveFileName", "camera_calib.xml", 2, PATH_MAX, SSHS_FLAGS_NORMAL,
		"The name of the file where to write the calculated calibration settings.");
	sshsNodeCreateInt(moduleNode, "captureDelay", 500000, 0, 60000000, SSHS_FLAGS_NORMAL,
		"Only use a frame for calibration if at least this much time has passed.");
	sshsNodeCreateInt(moduleNode, "minNumberOfPoints", 20, 3, 100, SSHS_FLAGS_NORMAL,
		"Minimum number of points to start calibration with.");
	sshsNodeCreateFloat(moduleNode, "maxTotalError", 0.30f, 0.0f, 1.0f, SSHS_FLAGS_NORMAL,
		"Maximum total average error allowed (in pixels).");
	sshsNodeCreateString(
		moduleNode, "calibrationPattern", "chessboard", 10, 21, SSHS_FLAGS_NORMAL, "Pattern to run calibration with.");
	sshsNodeCreateAttributeListOptions(
		moduleNode, "calibrationPattern", "chessboard,circlesGrid,asymmetricCirclesGrid", false);
	sshsNodeCreateInt(moduleNode, "boardWidth", 9, 1, 64, SSHS_FLAGS_NORMAL, "The size of the board (width).");
	sshsNodeCreateInt(moduleNode, "boardHeigth", 5, 1, 64, SSHS_FLAGS_NORMAL, "The size of the board (heigth).");
	sshsNodeCreateFloat(moduleNode, "boardSquareSize", 1.0f, 0.0f, 1000.0f, SSHS_FLAGS_NORMAL,
		"The size of a square in your defined unit (point, millimeter, etc.).");
	sshsNodeCreateFloat(moduleNode, "aspectRatio", 0.0f, 0.0f, 1.0f, SSHS_FLAGS_NORMAL, "The aspect ratio.");
	sshsNodeCreateBool(
		moduleNode, "assumeZeroTangentialDistortion", false, SSHS_FLAGS_NORMAL, "Assume zero tangential distortion.");
	sshsNodeCreateBool(
		moduleNode, "fixPrincipalPointAtCenter", false, SSHS_FLAGS_NORMAL, "Fix the principal point at the center.");
	sshsNodeCreateBool(
		moduleNode, "useFisheyeModel", false, SSHS_FLAGS_NORMAL, "Use fisheye camera model for calibration.");

	sshsNodeCreateBool(moduleNode, "doUndistortion", false, SSHS_FLAGS_NORMAL,
		"Do undistortion of incoming images using calibration loaded from file.");
	sshsNodeCreateString(moduleNode, "loadFileName", "camera_calib.xml", 2, PATH_MAX, SSHS_FLAGS_NORMAL,
		"The name of the file from which to load the calibration settings for undistortion.");
	sshsNodeCreateBool(moduleNode, "fitAllPixels", false, SSHS_FLAGS_NORMAL,
		"Whether to fit all the input pixels "
		"(black borders) or maximize the image, "
		"at the cost of loosing some pixels.");
}

static bool caerCameraCalibrationInit(caerModuleData moduleData) {
	// Both input packets (polarity and frame) must be from the same source, which
	// means inputSize should be 1 here (one module from which both come). If it isn't,
	// it means we connected the module wrongly.
	size_t inputsSize = caerMainloopModuleGetInputDeps(moduleData->moduleID, NULL);

	if (inputsSize != 1) {
		caerModuleLog(moduleData, CAER_LOG_ERROR,
			"Polarity and Frame inputs come from two different sources. Both must be from the same source!");
		return (false);
	}

	// Wait for input to be ready. All inputs, once they are up and running, will
	// have a valid sourceInfo node to query, especially if dealing with data.
	sshsNode sourceInfo = caerMainloopModuleGetSourceInfoForInput(moduleData->moduleID, 0);
	if (sourceInfo == NULL) {
		return (false);
	}

	// Update all settings.
	CameraCalibrationState state = moduleData->moduleState;

	state->settings.imageWidth  = U32T(sshsNodeGetInt(sourceInfo, "frameSizeX"));
	state->settings.imageHeigth = U32T(sshsNodeGetInt(sourceInfo, "frameSizeY"));

	updateSettings(moduleData);

	// Initialize C++ class for OpenCV integration.
	state->cpp_class = calibration_init(&state->settings);
	if (state->cpp_class == NULL) {
		return (false);
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	return (true);
}

static void updateSettings(caerModuleData moduleData) {
	CameraCalibrationState state = moduleData->moduleState;

	// Get current config settings.
	state->settings.doCalibration     = sshsNodeGetBool(moduleData->moduleNode, "doCalibration");
	state->settings.captureDelay      = U32T(sshsNodeGetInt(moduleData->moduleNode, "captureDelay"));
	state->settings.minNumberOfPoints = U32T(sshsNodeGetInt(moduleData->moduleNode, "minNumberOfPoints"));
	state->settings.maxTotalError     = sshsNodeGetFloat(moduleData->moduleNode, "maxTotalError");
	state->settings.boardWidth        = U32T(sshsNodeGetInt(moduleData->moduleNode, "boardWidth"));
	state->settings.boardHeigth       = U32T(sshsNodeGetInt(moduleData->moduleNode, "boardHeigth"));
	state->settings.boardSquareSize   = sshsNodeGetFloat(moduleData->moduleNode, "boardSquareSize");
	state->settings.aspectRatio       = sshsNodeGetFloat(moduleData->moduleNode, "aspectRatio");
	state->settings.assumeZeroTangentialDistortion
		= sshsNodeGetBool(moduleData->moduleNode, "assumeZeroTangentialDistortion");
	state->settings.fixPrincipalPointAtCenter = sshsNodeGetBool(moduleData->moduleNode, "fixPrincipalPointAtCenter");
	state->settings.useFisheyeModel           = sshsNodeGetBool(moduleData->moduleNode, "useFisheyeModel");
	state->settings.doUndistortion            = sshsNodeGetBool(moduleData->moduleNode, "doUndistortion");
	state->settings.fitAllPixels              = sshsNodeGetBool(moduleData->moduleNode, "fitAllPixels");

	// Parse calibration pattern string.
	char *calibPattern = sshsNodeGetString(moduleData->moduleNode, "calibrationPattern");

	if (caerStrEquals(calibPattern, "chessboard")) {
		state->settings.calibrationPattern = CAMCALIB_CHESSBOARD;
	}
	else if (caerStrEquals(calibPattern, "circlesGrid")) {
		state->settings.calibrationPattern = CAMCALIB_CIRCLES_GRID;
	}
	else if (caerStrEquals(calibPattern, "asymmetricCirclesGrid")) {
		state->settings.calibrationPattern = CAMCALIB_ASYMMETRIC_CIRCLES_GRID;
	}
	else {
		caerModuleLog(moduleData, CAER_LOG_ERROR,
			"Invalid calibration pattern defined. Select one of: 'chessboard', "
			"'circlesGrid' or 'asymmetricCirclesGrid'. Defaulting to "
			"'chessboard'.");

		state->settings.calibrationPattern = CAMCALIB_CHESSBOARD;
	}

	free(calibPattern);

	// Get file strings.
	state->settings.saveFileName = sshsNodeGetString(moduleData->moduleNode, "saveFileName");
	state->settings.loadFileName = sshsNodeGetString(moduleData->moduleNode, "loadFileName");
}

static void caerCameraCalibrationConfig(caerModuleData moduleData) {
	CameraCalibrationState state = moduleData->moduleState;

	// Free filename strings, get reloaded in next step.
	free(state->settings.saveFileName);
	free(state->settings.loadFileName);

	// Reload all local settings.
	updateSettings(moduleData);

	// Update the C++ internal state, based on new settings.
	calibration_updateSettings(state->cpp_class);

	// Reset calibration status after any config change.
	state->lastFrameTimestamp   = 0;
	state->lastFoundPoints      = 0;
	state->calibrationCompleted = false;
	state->calibrationLoaded    = false;
}

static void caerCameraCalibrationExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	CameraCalibrationState state = moduleData->moduleState;

	calibration_destroy(state->cpp_class);

	free(state->settings.saveFileName);
	free(state->settings.loadFileName);
}

static void caerCameraCalibrationRun(
	caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	caerPolarityEventPacket polarity
		= (caerPolarityEventPacket) caerEventPacketContainerFindEventPacketByType(in, POLARITY_EVENT);
	caerFrameEventPacket frame = (caerFrameEventPacket) caerEventPacketContainerFindEventPacketByType(in, FRAME_EVENT);

	CameraCalibrationState state = moduleData->moduleState;

	// Calibration is done only using frames.
	if (state->settings.doCalibration && !state->calibrationCompleted && frame != NULL) {
		CAER_FRAME_ITERATOR_VALID_START(frame)
		// Only work on new frames if enough time has passed between this and the last used one.
		uint64_t currTimestamp = U64T(caerFrameEventGetTSStartOfFrame64(caerFrameIteratorElement, frame));

		// If enough time has passed, try to add a new point set.
		if ((currTimestamp - state->lastFrameTimestamp) >= state->settings.captureDelay) {
			state->lastFrameTimestamp = currTimestamp;

			bool foundPoint = calibration_findNewPoints(state->cpp_class, caerFrameIteratorElement);
			caerModuleLog(moduleData, CAER_LOG_WARNING, "Searching for new point set, result = %d.", foundPoint);
		}
		CAER_FRAME_ITERATOR_VALID_END

		// If enough points have been found in this round, try doing calibration.
		size_t foundPoints = calibration_foundPoints(state->cpp_class);

		if (foundPoints >= state->settings.minNumberOfPoints && foundPoints > state->lastFoundPoints) {
			state->lastFoundPoints = foundPoints;

			double totalAvgError;
			state->calibrationCompleted = calibration_runCalibrationAndSave(state->cpp_class, &totalAvgError);
			caerModuleLog(moduleData, CAER_LOG_WARNING, "Executing calibration, result = %d, error = %f.",
				state->calibrationCompleted, totalAvgError);
		}
	}

	// At this point we always try to load the calibration settings for undistortion.
	// Maybe they just got created or exist from a previous run.
	if (state->settings.doUndistortion && !state->calibrationLoaded) {
		state->calibrationLoaded = calibration_loadUndistortMatrices(state->cpp_class);
	}

	// Undistortion can be applied to both frames and events.
	if (state->settings.doUndistortion && state->calibrationLoaded) {
		if (frame != NULL) {
			CAER_FRAME_ITERATOR_VALID_START(frame)
			calibration_undistortFrame(state->cpp_class, caerFrameIteratorElement);
			CAER_FRAME_ITERATOR_VALID_END
		}

		if (polarity != NULL) {
			CAER_POLARITY_ITERATOR_VALID_START(polarity)
			calibration_undistortEvent(state->cpp_class, caerPolarityIteratorElement, polarity);
			CAER_POLARITY_ITERATOR_VALID_END
		}
	}
}
