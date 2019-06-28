#include "wrapper.h"
#include "../reader/wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_activity_sink_config_init(sshsNode module_node);
static bool benchmark_activity_sink_init(caerModuleData module_data);
static void benchmark_activity_sink_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_activity_sink_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_activity_sink_functions = {
    .moduleConfigInit = &benchmark_activity_sink_config_init,
    .moduleInit = &benchmark_activity_sink_init,
    .moduleRun = &benchmark_activity_sink_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_activity_sink_exit,
};

static void benchmark_activity_sink_config_init(sshsNode module_node) {
    sshsNodeCreateString(
        module_node,
        "filename",
        "",
        0,
        PATH_MAX,
        SSHS_FLAGS_NORMAL,
		"output log file");
}

static const struct caer_event_stream_in benchmark_activity_sink_inputs[] = {{
    .type = POINT2D_EVENT,
    .number = 1,
    .readOnly = true,
}};

static const struct caer_module_info benchmark_activity_sink_info = {
    .version = 1,
    .name = "benchmark_activity_sink",
    .description = "Stores events in RAM",
    .type = CAER_MODULE_OUTPUT,
    .memSize = sizeof(struct benchmark_activity_sink_state_struct),
    .functions = &benchmark_activity_sink_functions,
    .inputStreams = benchmark_activity_sink_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_activity_sink_inputs),
    .outputStreams = NULL,
    .outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_activity_sink_info;
}

static bool benchmark_activity_sink_init(caerModuleData module_data) {
    char* filename = sshsNodeGetString(module_data->moduleNode, "filename");
    benchmark_activity_sink_state state = module_data->moduleState;
    benchmark_reader_state reader_state = caerMainloopGetSourceState(1);
    if (reader_state == NULL) {
        return false;
    }
    state->benchmark_activity_sink_instance = benchmark_activity_sink_construct(
        filename,
        benchmark_reader_number_of_packets(reader_state->benchmark_reader_instance),
        benchmark_reader_number_of_events(reader_state->benchmark_reader_instance));
    if (state->benchmark_activity_sink_instance == NULL) {
        return false;
    }
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    return true;
}

static void benchmark_activity_sink_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    UNUSED_ARGUMENT(out);
    benchmark_activity_sink_state state = module_data->moduleState;
    benchmark_activity_sink_add_packet(state->benchmark_activity_sink_instance, in);
}

static void benchmark_activity_sink_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_activity_sink_state state = module_data->moduleState;
    benchmark_activity_sink_destruct(state->benchmark_activity_sink_instance);
}
