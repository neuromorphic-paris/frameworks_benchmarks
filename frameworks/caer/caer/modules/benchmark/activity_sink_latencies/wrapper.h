#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_activity_sink_latencies benchmark_activity_sink_latencies;

benchmark_activity_sink_latencies* benchmark_activity_sink_latencies_construct(char* filename, size_t number_of_packets, size_t number_of_events);
void benchmark_activity_sink_latencies_destruct(benchmark_activity_sink_latencies* benchmark_activity_sink_latencies_instance);
void benchmark_activity_sink_latencies_add_packet(benchmark_activity_sink_latencies* benchmark_activity_sink_latencies_instance, caerEventPacketContainer container);

struct benchmark_activity_sink_latencies_state_struct {
    struct benchmark_activity_sink_latencies* benchmark_activity_sink_latencies_instance;
};
typedef struct benchmark_activity_sink_latencies_state_struct* benchmark_activity_sink_latencies_state;

#ifdef __cplusplus
}
#endif
