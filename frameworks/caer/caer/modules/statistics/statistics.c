#include "statistics.h"

#include "caer-sdk/mainloop.h"

static void statisticsModuleConfigInit(sshsNode moduleNode);
static bool statisticsModuleInit(caerModuleData moduleData);
static void statisticsModuleRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void statisticsModuleReset(caerModuleData moduleData, int16_t resetCallSourceID);

static const struct caer_module_functions StatisticsFunctions = {
	.moduleConfigInit = &statisticsModuleConfigInit,
	.moduleInit       = &statisticsModuleInit,
	.moduleRun        = &statisticsModuleRun,
	.moduleConfig     = NULL,
	.moduleExit       = NULL,
	.moduleReset      = &statisticsModuleReset,
};

static const struct caer_event_stream_in StatisticsInputs[] = {{
	.type     = -1,
	.number   = 1,
	.readOnly = true,
}};

static const struct caer_module_info StatisticsInfo = {
	.version           = 1,
	.name              = "Statistics",
	.description       = "Display statistics on events.",
	.type              = CAER_MODULE_OUTPUT,
	.memSize           = sizeof(struct caer_statistics_state),
	.functions         = &StatisticsFunctions,
	.inputStreams      = StatisticsInputs,
	.inputStreamsSize  = CAER_EVENT_STREAM_IN_SIZE(StatisticsInputs),
	.outputStreams     = NULL,
	.outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo(void) {
	return (&StatisticsInfo);
}

static void statisticsModuleConfigInit(sshsNode moduleNode) {
	sshsNodeCreateLong(moduleNode, "divisionFactor", 1000, 1, INT64_MAX, SSHS_FLAGS_NORMAL,
		"Division factor for statistics display, to get Kilo/Mega/... events shown.");

	sshsNodeCreateLong(moduleNode, "eventsTotal", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Number of events per second.");

	sshsNodeCreateLong(moduleNode, "eventsValid", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Number of valid events per second.");

	sshsNodeCreateLong(moduleNode, "packetTSDiff", 0, 0, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT,
		"Maximum time difference (in µs) between consecutive packets.");
}

static bool statisticsModuleInit(caerModuleData moduleData) {
	caerStatisticsState state = moduleData->moduleState;

	caerStatisticsInit(state);

	// Configurable division factor.
	state->divisionFactor = U64T(sshsNodeGetLong(moduleData->moduleNode, "divisionFactor"));

	return (true);
}

static void statisticsModuleRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(out);

	// Interpret variable arguments (same as above in main function).
	caerEventPacketHeaderConst packetHeader = caerEventPacketContainerGetEventPacketConst(in, 0);

	caerStatisticsState state = moduleData->moduleState;

	if (caerStatisticsUpdate(packetHeader, state)) {
		sshsNodeUpdateReadOnlyAttribute(moduleData->moduleNode, "eventsTotal", SSHS_LONG,
			(union sshs_node_attr_value){.ilong = state->currStatsEventsTotal});
		sshsNodeUpdateReadOnlyAttribute(moduleData->moduleNode, "eventsValid", SSHS_LONG,
			(union sshs_node_attr_value){.ilong = state->currStatsEventsValid});
		sshsNodeUpdateReadOnlyAttribute(moduleData->moduleNode, "packetTSDiff", SSHS_LONG,
			(union sshs_node_attr_value){.ilong = state->currStatsPacketTSDiff});
	}
}

static void statisticsModuleReset(caerModuleData moduleData, int16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	caerStatisticsState state = moduleData->moduleState;

	caerStatisticsReset(state);
}
