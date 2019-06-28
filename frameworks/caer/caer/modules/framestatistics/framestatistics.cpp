#include "caer-sdk/mainloop.h"

#include <libcaercpp/events/frame.hpp>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

struct caer_frame_statistics_state {
	int numBins;
	int roiRegion;
};

typedef struct caer_frame_statistics_state *caerFrameStatisticsState;

static void caerFrameStatisticsConfigInit(sshsNode moduleNode);
static bool caerFrameStatisticsInit(caerModuleData moduleData);
static void caerFrameStatisticsRun(
	caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerFrameStatisticsExit(caerModuleData moduleData);
static void caerFrameStatisticsConfig(caerModuleData moduleData);

static const struct caer_module_functions FrameStatisticsFunctions
	= {.moduleConfigInit = &caerFrameStatisticsConfigInit,
		.moduleInit      = &caerFrameStatisticsInit,
		.moduleRun       = &caerFrameStatisticsRun,
		.moduleConfig    = &caerFrameStatisticsConfig,
		.moduleExit      = &caerFrameStatisticsExit,
		.moduleReset     = NULL};

static const struct caer_event_stream_in FrameStatisticsInputs[]
	= {{.type = FRAME_EVENT, .number = 1, .readOnly = true}};

static const struct caer_module_info FrameStatisticsInfo = {.version = 1,
	.name                                                            = "FrameStatistics",
	.description                                                     = "Display statistics on frames (histogram).",
	.type                                                            = CAER_MODULE_OUTPUT,
	.memSize                                                         = sizeof(struct caer_frame_statistics_state),
	.functions                                                       = &FrameStatisticsFunctions,
	.inputStreamsSize                                                = CAER_EVENT_STREAM_IN_SIZE(FrameStatisticsInputs),
	.inputStreams                                                    = FrameStatisticsInputs,
	.outputStreamsSize                                               = 0,
	.outputStreams                                                   = NULL};

caerModuleInfo caerModuleGetInfo(void) {
	return (&FrameStatisticsInfo);
}

static void caerFrameStatisticsConfigInit(sshsNode moduleNode) {
	sshsNodeCreate(moduleNode, "numBins", 1024, 4, UINT16_MAX + 1, SSHS_FLAGS_NORMAL,
		"Number of bins in which to divide values up.");
	sshsNodeCreate(moduleNode, "roiRegion", 0, 0, 7, SSHS_FLAGS_NORMAL, "Selects which ROI region to display.");
	sshsNodeCreateInt(moduleNode, "windowPositionX", 20, 0, UINT16_MAX, SSHS_FLAGS_NORMAL,
		"Position of window on screen (X coordinate).");
	sshsNodeCreateInt(moduleNode, "windowPositionY", 20, 0, UINT16_MAX, SSHS_FLAGS_NORMAL,
		"Position of window on screen (Y coordinate).");
}

static bool caerFrameStatisticsInit(caerModuleData moduleData) {
	// Get configuration.
	caerFrameStatisticsConfig(moduleData);

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	cv::namedWindow(moduleData->moduleSubSystemString,
		cv::WindowFlags::WINDOW_AUTOSIZE | cv::WindowFlags::WINDOW_KEEPRATIO | cv::WindowFlags::WINDOW_GUI_EXPANDED);

	return (true);
}

static void caerFrameStatisticsRun(
	caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	caerFrameEventPacket inPacket
		= reinterpret_cast<caerFrameEventPacket>(caerEventPacketContainerGetEventPacket(in, 0));

	// Only process packets with content.
	if (inPacket == nullptr) {
		return;
	}

	const libcaer::events::FrameEventPacket frames(inPacket, false);

	caerFrameStatisticsState state = static_cast<caerFrameStatisticsState>(moduleData->moduleState);

	for (const auto &frame : frames) {
		if ((!frame.isValid()) || (frame.getROIIdentifier() != state->roiRegion)) {
			continue;
		}

		const cv::Mat frameOpenCV = frame.getOpenCVMat(false);

		// Calculate histogram, full uint16 range.
		const float range[]    = {0, UINT16_MAX + 1};
		const float *histRange = {range};

		cv::Mat hist;
		cv::calcHist(&frameOpenCV, 1, nullptr, cv::Mat(), hist, 1, &state->numBins, &histRange, true, false);

		// Generate histogram image, with N x N/3 pixels.
		int hist_w = state->numBins;
		int hist_h = state->numBins / 3;

		cv::Mat histImage(hist_h, hist_w, CV_8UC1, cv::Scalar(0));

		// Normalize the result to [0, histImage.rows].
		cv::normalize(hist, hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat());

		// Draw the histogram.
		for (int i = 1; i < state->numBins; i++) {
			cv::line(histImage, cv::Point(i - 1, hist_h - cvRound(hist.at<float>(i - 1))),
				cv::Point(i, hist_h - cvRound(hist.at<float>(i))), cv::Scalar(255, 255, 255), 2, 8, 0);
		}

		// Simple display, just use OpenCV GUI.
		cv::imshow(moduleData->moduleSubSystemString, histImage);
		cv::waitKey(1);
	}
}

static void caerFrameStatisticsExit(caerModuleData moduleData) {
	cv::destroyWindow(moduleData->moduleSubSystemString);

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);
}

static void caerFrameStatisticsConfig(caerModuleData moduleData) {
	caerFrameStatisticsState state = (caerFrameStatisticsState) moduleData->moduleState;

	state->numBins   = sshsNodeGetInt(moduleData->moduleNode, "numBins");
	state->roiRegion = sshsNodeGetInt(moduleData->moduleNode, "roiRegion");

	int posX = sshsNodeGetInt(moduleData->moduleNode, "windowPositionX");
	int posY = sshsNodeGetInt(moduleData->moduleNode, "windowPositionY");

	cv::moveWindow(moduleData->moduleSubSystemString, posX, posY);
}
