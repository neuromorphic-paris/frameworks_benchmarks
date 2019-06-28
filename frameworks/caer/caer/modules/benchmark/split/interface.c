#include "wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_split_config_init(sshsNode module_node);
static bool benchmark_split_init(caerModuleData module_data);
static void benchmark_split_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_split_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_split_functions = {
    .moduleConfigInit = &benchmark_split_config_init,
    .moduleInit = &benchmark_split_init,
    .moduleRun = &benchmark_split_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_split_exit,
};

static void benchmark_split_config_init(sshsNode module_node) {}

static const struct caer_event_stream_in benchmark_split_inputs[] = {{
    .type = POLARITY_EVENT,
    .number = 1,
    .readOnly = false,
}};

static const struct caer_module_info benchmark_split_info = {
    .version = 1,
    .name = "benchmark_split",
    .description = "propagates only events with active neighbours",
    .type = CAER_MODULE_PROCESSOR,
    .memSize = sizeof(struct benchmark_split_state_struct),
    .functions = &benchmark_split_functions,
    .inputStreams = benchmark_split_inputs,
    .inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(benchmark_split_inputs),
    .outputStreams = NULL,
    .outputStreamsSize = 0,
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_split_info;
}

static bool benchmark_split_init(caerModuleData module_data) {
    benchmark_split_state state = module_data->moduleState;
    state->benchmark_split_instance = benchmark_split_construct();
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    caerMainloopDataNotifyIncrease(NULL);
    return true;
}

static void benchmark_split_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    UNUSED_ARGUMENT(out);
    benchmark_split_state state = module_data->moduleState;
    benchmark_split_handle_packet(state->benchmark_split_instance, in);
}

static void benchmark_split_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_split_state state = module_data->moduleState;
    benchmark_split_destruct(state->benchmark_split_instance);
}
