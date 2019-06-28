#include "wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_compute_flow_config_init(sshsNode module_node);
static bool benchmark_compute_flow_init(caerModuleData module_data);
static void benchmark_compute_flow_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_compute_flow_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_compute_flow_functions = {
    .moduleConfigInit = &benchmark_compute_flow_config_init,
    .moduleInit = &benchmark_compute_flow_init,
    .moduleRun = &benchmark_compute_flow_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_compute_flow_exit,
};

static void benchmark_compute_flow_config_init(sshsNode module_node) {
    sshsNodeCreateInt(module_node, "width", 304, 0, 304, SSHS_FLAGS_NORMAL, "window width");
    sshsNodeCreateInt(module_node, "height", 240, 0, 240, SSHS_FLAGS_NORMAL, "window height");
    sshsNodeCreateInt(module_node, "spatial_window", 3, 0, 10, SSHS_FLAGS_NORMAL, "spatial radius in pixels");
    sshsNodeCreateInt(module_node, "temporal_window", 1e4, 0, 1e7, SSHS_FLAGS_NORMAL, "temporal context");
    sshsNodeCreateInt(module_node, "minimum_number_of_events", 8, 0, 22, SSHS_FLAGS_NORMAL, "minimum number of events to trigger a flow computation");
}

static const struct caer_event_stream_in benchmark_compute_flow_inputs[] = {{
    .type = POLARITY_EVENT,
    .number = 1,
    .readOnly = true,
}};

static const struct caer_event_stream_out benchmark_compute_flow_outputs[] = {{
    .type = POINT3D_EVENT,
}};

static const struct caer_module_info benchmark_compute_flow_info = {
    .version = 1,
    .name = "benchmark_compute_flow",
    .description = "calculates the optical flow",
    .type = CAER_MODULE_PROCESSOR,
    .memSize = sizeof(struct benchmark_compute_flow_state_struct),
    .functions = &benchmark_compute_flow_functions,
    .inputStreams = benchmark_compute_flow_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_compute_flow_inputs),
    .outputStreams = benchmark_compute_flow_outputs,
    .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(benchmark_compute_flow_outputs),
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_compute_flow_info;
}

static bool benchmark_compute_flow_init(caerModuleData module_data) {
    benchmark_compute_flow_state state = module_data->moduleState;
    state->benchmark_compute_flow_instance = benchmark_compute_flow_construct(
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "width")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "height")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "spatial_window")),
        (uint64_t)(sshsNodeGetInt(module_data->moduleNode, "temporal_window")),
        (size_t)(sshsNodeGetInt(module_data->moduleNode, "minimum_number_of_events")));
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    caerMainloopDataNotifyIncrease(NULL);
    return true;
}

static void benchmark_compute_flow_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    benchmark_compute_flow_state state = module_data->moduleState;
    benchmark_compute_flow_handle_packet(state->benchmark_compute_flow_instance, in, out);
}

static void benchmark_compute_flow_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_compute_flow_state state = module_data->moduleState;
    benchmark_compute_flow_destruct(state->benchmark_compute_flow_instance);
}
