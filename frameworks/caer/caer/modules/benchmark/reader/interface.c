#include "wrapper.h"
#include <caer-sdk/cross/portable_io.h>
#include <caer-sdk/mainloop.h>
#include <time.h>

static void benchmark_reader_config_init(sshsNode module_node);
static bool benchmark_reader_init(caerModuleData module_data);
static void benchmark_reader_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out);
static void benchmark_reader_exit(caerModuleData module_data);

static struct caer_module_functions benchmark_reader_functions = {
    .moduleConfigInit = &benchmark_reader_config_init,
    .moduleInit = &benchmark_reader_init,
    .moduleRun = &benchmark_reader_run,
    .moduleConfig = NULL,
    .moduleExit = &benchmark_reader_exit,
};

static void benchmark_reader_config_init(sshsNode module_node) {
    sshsNodeCreateString(
        module_node,
        "filename",
        "",
        0,
        PATH_MAX,
        SSHS_FLAGS_NORMAL,
		"input Event Stream file");
    sshsNodeCreateString(
        module_node,
        "output_filename",
        "",
        0,
        PATH_MAX,
        SSHS_FLAGS_NORMAL,
		"output log file");
}

static const struct caer_event_stream_out benchmark_reader_outputs[] = {
    {.type = POLARITY_EVENT},
};

static const struct caer_module_info benchmark_reader_info = {
    .version = 1,
    .name = "benchmark_reader",
    .description = "Dispatches events pre-loaded into RAM.",
    .type = CAER_MODULE_INPUT,
    .memSize = sizeof(struct benchmark_reader_state_struct),
    .functions = &benchmark_reader_functions,
    .inputStreams = NULL,
    .inputStreamsSize = 0,
    .outputStreams = benchmark_reader_outputs,
    .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(benchmark_reader_outputs),
};

caerModuleInfo caerModuleGetInfo() {
    return &benchmark_reader_info;
}

static bool benchmark_reader_init(caerModuleData module_data) {
    char* filename = sshsNodeGetString(module_data->moduleNode, "filename");
    char* output_filename = sshsNodeGetString(module_data->moduleNode, "output_filename");
    benchmark_reader_state state = module_data->moduleState;
    state->benchmark_reader_instance = benchmark_reader_construct(filename, output_filename);
    state->ended = false;
    if (state->benchmark_reader_instance == NULL) {
        return false;
    }
    sshsNodeAddAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    caerMainloopDataNotifyIncrease(NULL);
    return true;
}

static void benchmark_reader_run(caerModuleData module_data, caerEventPacketContainer in, caerEventPacketContainer* out) {
    UNUSED_ARGUMENT(in);
    benchmark_reader_state state = module_data->moduleState;
    *out = benchmark_reader_next_packet(state->benchmark_reader_instance);
    if (*out == NULL && !state->ended) {
        state->ended = true;
        caerMainloopDataNotifyDecrease(NULL);
    }
}

static void benchmark_reader_exit(caerModuleData module_data) {
    sshsNodeRemoveAttributeListener(module_data->moduleNode, module_data, &caerModuleConfigDefaultListener);
    benchmark_reader_state state = module_data->moduleState;
    benchmark_reader_destruct(state->benchmark_reader_instance);
}
