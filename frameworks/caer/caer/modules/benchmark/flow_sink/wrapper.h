#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_flow_sink benchmark_flow_sink;

benchmark_flow_sink* benchmark_flow_sink_construct(char* filename, size_t number_of_packets, size_t number_of_events);
void benchmark_flow_sink_destruct(benchmark_flow_sink* benchmark_flow_sink_instance);
void benchmark_flow_sink_add_packet(benchmark_flow_sink* benchmark_flow_sink_instance, caerEventPacketContainer container);

struct benchmark_flow_sink_state_struct {
    struct benchmark_flow_sink* benchmark_flow_sink_instance;
};
typedef struct benchmark_flow_sink_state_struct* benchmark_flow_sink_state;

#ifdef __cplusplus
}
#endif
