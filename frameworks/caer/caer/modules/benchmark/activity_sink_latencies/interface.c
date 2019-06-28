#include "wrapper.h"
#include "../reader_latencies/wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_activity_sink_latencies_config_init(sshsNode module_node);
static bool benchmark_activity_sink_latencies_init(caerModuleData module_data);
static void benchmark_activity_sink_latencies_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_activity_sink_latencies_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_activity_sink_latencies_functions = {
    .moduleConfigInit = &benchmark_activity_sink_latencies_config_init,
    .moduleInit = &benchmark_activity_sink_latencies_init,
    .moduleRun = &benchmark_activity_sink_latencies_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_activity_sink_latencies_exit,
};

static void benchmark_activity_sink_latencies_config_init(sshsNode module_node) {
    sshsNodeCreateString(
        module_node,
        "filename",
        "",
        0,
        PATH_MAX,
        SSHS_FLAGS_NORMAL,
		"output log file");
}

static const struct caer_event_stream_in benchmark_activity_sink_latencies_inputs[] = {{
    .type = POINT2D_EVENT,
    .number = 1,
    .readOnly = true,
}};

static const struct caer_module_info benchmark_activity_sink_latencies_info = {
    .version = 1,
    .name = "benchmark_activity_sink_latencies",
    .description = "Stores events in RAM",
    .type = CAER_MODULE_OUTPUT,
    .memSize = sizeof(struct benchmark_activity_sink_latencies_state_struct),
    .functions = &benchmark_activity_sink_latencies_functions,
    .inputStreams = benchmark_activity_sink_latencies_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_activity_sink_latencies_inputs),
    .outputStreams = NULL,
    .outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_activity_sink_latencies_info;
}

static bool benchmark_activity_sink_latencies_init(caerModuleData module_data) {
    char* filename = sshsNodeGetString(module_data->moduleNode, "filename");
    benchmark_activity_sink_latencies_state state = module_data->moduleState;
    benchmark_reader_latencies_state reader_latencies_state = caerMainloopGetSourceState(1);
    if (reader_latencies_state == NULL) {
        return false;
    }
    state->benchmark_activity_sink_latencies_instance = benchmark_activity_sink_latencies_construct(
        filename,
        benchmark_reader_latencies_number_of_packets(reader_latencies_state->benchmark_reader_latencies_instance),
        benchmark_reader_latencies_number_of_events(reader_latencies_state->benchmark_reader_latencies_instance));
    if (state->benchmark_activity_sink_latencies_instance == NULL) {
        return false;
    }
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    return true;
}

static void benchmark_activity_sink_latencies_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    UNUSED_ARGUMENT(out);
    benchmark_activity_sink_latencies_state state = module_data->moduleState;
    benchmark_activity_sink_latencies_add_packet(state->benchmark_activity_sink_latencies_instance, in);
}

static void benchmark_activity_sink_latencies_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_activity_sink_latencies_state state = module_data->moduleState;
    benchmark_activity_sink_latencies_destruct(state->benchmark_activity_sink_latencies_instance);
}
