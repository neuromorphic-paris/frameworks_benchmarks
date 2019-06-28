#include "wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_select_rectangle_config_init(sshsNode module_node);
static bool benchmark_select_rectangle_init(caerModuleData module_data);
static void benchmark_select_rectangle_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_select_rectangle_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_select_rectangle_functions = {
    .moduleConfigInit = &benchmark_select_rectangle_config_init,
    .moduleInit = &benchmark_select_rectangle_init,
    .moduleRun = &benchmark_select_rectangle_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_select_rectangle_exit,
};

static void benchmark_select_rectangle_config_init(sshsNode module_node) {
    sshsNodeCreateInt(module_node, "left", 0, 0, 303, SSHS_FLAGS_NORMAL, "bottom-left corner's x coordinate");
    sshsNodeCreateInt(module_node, "bottom", 0, 0, 239, SSHS_FLAGS_NORMAL, "bottom-left corner's y coordinate");
    sshsNodeCreateInt(module_node, "width", 304, 0, 304, SSHS_FLAGS_NORMAL, "window width");
    sshsNodeCreateInt(module_node, "height", 240, 0, 240, SSHS_FLAGS_NORMAL, "window height");
}

static const struct caer_event_stream_in benchmark_select_rectangle_inputs[] = {{
    .type = POLARITY_EVENT,
    .number = 1,
    .readOnly = false,
}};

static const struct caer_module_info benchmark_select_rectangle_info = {
    .version = 1,
    .name = "benchmark_select_rectangle",
    .description = "propagates only events within the given spatial window",
    .type = CAER_MODULE_PROCESSOR,
    .memSize = sizeof(struct benchmark_select_rectangle_state_struct),
    .functions = &benchmark_select_rectangle_functions,
    .inputStreams = benchmark_select_rectangle_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_select_rectangle_inputs),
    .outputStreams = NULL,
    .outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_select_rectangle_info;
}

static bool benchmark_select_rectangle_init(caerModuleData module_data) {
    benchmark_select_rectangle_state state = module_data->moduleState;
    state->benchmark_select_rectangle_instance = benchmark_select_rectangle_construct(
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "left")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "bottom")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "width")),
        (uint16_t)(sshsNodeGetInt(module_data->moduleNode, "height")));
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    caerMainloopDataNotifyIncrease(NULL);
    return true;
}

static void benchmark_select_rectangle_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    UNUSED_ARGUMENT(out);
    benchmark_select_rectangle_state state = module_data->moduleState;
    benchmark_select_rectangle_handle_packet(state->benchmark_select_rectangle_instance, in);
}

static void benchmark_select_rectangle_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_select_rectangle_state state = module_data->moduleState;
    benchmark_select_rectangle_destruct(state->benchmark_select_rectangle_instance);
}
