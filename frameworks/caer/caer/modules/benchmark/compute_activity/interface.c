#include "wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_compute_activity_config_init(sshsNode module_node);
static bool benchmark_compute_activity_init(caerModuleData module_data);
static void benchmark_compute_activity_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_compute_activity_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_compute_activity_functions = {
    .moduleConfigInit = &benchmark_compute_activity_config_init,
    .moduleInit = &benchmark_compute_activity_init,
    .moduleRun = &benchmark_compute_activity_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_compute_activity_exit,
};

static void benchmark_compute_activity_config_init(sshsNode module_node) {
    sshsNodeCreateInt(module_node, "width", 304, 0, 304, SSHS_FLAGS_NORMAL, "window width");
    sshsNodeCreateInt(module_node, "height", 240, 0, 240, SSHS_FLAGS_NORMAL, "window height");
    sshsNodeCreateFloat(module_node, "decay", 1e3, 0, 1e7, SSHS_FLAGS_NORMAL, "exponential decay");
}

static const struct caer_event_stream_in benchmark_compute_activity_inputs[] = {{
    .type = POINT3D_EVENT,
    .number = 1,
    .readOnly = true,
}};

static const struct caer_event_stream_out benchmark_compute_activity_outputs[] = {{
    .type = POINT2D_EVENT,
}};

static const struct caer_module_info benchmark_compute_activity_info = {
    .version = 1,
    .name = "benchmark_compute_activity",
    .description = "calculates the activity",
    .type = CAER_MODULE_PROCESSOR,
    .memSize = sizeof(struct benchmark_compute_activity_state_struct),
    .functions = &benchmark_compute_activity_functions,
    .inputStreams = benchmark_compute_activity_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_compute_activity_inputs),
    .outputStreams = benchmark_compute_activity_outputs,
    .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(benchmark_compute_activity_outputs),
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_compute_activity_info;
}

static bool benchmark_compute_activity_init(caerModuleData module_data) {
    benchmark_compute_activity_state state = module_data->moduleState;
    state->benchmark_compute_activity_instance = benchmark_compute_activity_construct(
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "width")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "height")),
        (float)(sshsNodeGetFloat(module_data->moduleNode, "decay")));
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    caerMainloopDataNotifyIncrease(NULL);
    return true;
}

static void benchmark_compute_activity_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    benchmark_compute_activity_state state = module_data->moduleState;
    benchmark_compute_activity_handle_packet(state->benchmark_compute_activity_instance, in, out);
}

static void benchmark_compute_activity_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_compute_activity_state state = module_data->moduleState;
    benchmark_compute_activity_destruct(state->benchmark_compute_activity_instance);
}
