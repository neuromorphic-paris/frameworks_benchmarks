#include "wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_mask_isolated_config_init(sshsNode module_node);
static bool benchmark_mask_isolated_init(caerModuleData module_data);
static void benchmark_mask_isolated_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_mask_isolated_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_mask_isolated_functions = {
    .moduleConfigInit = &benchmark_mask_isolated_config_init,
    .moduleInit = &benchmark_mask_isolated_init,
    .moduleRun = &benchmark_mask_isolated_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_mask_isolated_exit,
};

static void benchmark_mask_isolated_config_init(sshsNode module_node) {
    sshsNodeCreateInt(module_node, "width", 304, 0, 304, SSHS_FLAGS_NORMAL, "sensor width");
    sshsNodeCreateInt(module_node, "height", 240, 0, 240, SSHS_FLAGS_NORMAL, "sensor height");
    sshsNodeCreateInt(module_node, "temporal_window", 1e3, 0, 1e7, SSHS_FLAGS_NORMAL, "temporal context");
}

static const struct caer_event_stream_in benchmark_mask_isolated_inputs[] = {{
    .type = POLARITY_EVENT,
    .number = 1,
    .readOnly = false,
}};

static const struct caer_module_info benchmark_mask_isolated_info = {
    .version = 1,
    .name = "benchmark_mask_isolated",
    .description = "propagates only events with active neighbours",
    .type = CAER_MODULE_PROCESSOR,
    .memSize = sizeof(struct benchmark_mask_isolated_state_struct),
    .functions = &benchmark_mask_isolated_functions,
    .inputStreams = benchmark_mask_isolated_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_mask_isolated_inputs),
    .outputStreams = NULL,
    .outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_mask_isolated_info;
}

static bool benchmark_mask_isolated_init(caerModuleData module_data) {
    benchmark_mask_isolated_state state = module_data->moduleState;
    state->benchmark_mask_isolated_instance = benchmark_mask_isolated_construct(
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "width")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "height")),
        (uint64_t)(sshsNodeGetInt(module_data->moduleNode, "temporal_window")));
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    caerMainloopDataNotifyIncrease(NULL);
    return true;
}

static void benchmark_mask_isolated_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    UNUSED_ARGUMENT(out);
    benchmark_mask_isolated_state state = module_data->moduleState;
    benchmark_mask_isolated_handle_packet(state->benchmark_mask_isolated_instance, in);
}

static void benchmark_mask_isolated_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_mask_isolated_state state = module_data->moduleState;
    benchmark_mask_isolated_destruct(state->benchmark_mask_isolated_instance);
}
