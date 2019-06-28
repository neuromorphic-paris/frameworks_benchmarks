#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_compute_flow benchmark_compute_flow;

benchmark_compute_flow* benchmark_compute_flow_construct(
    uint16_t width,
    uint16_t height,
    uint16_t spatial_window,
    uint64_t temporal_window,
    size_t minimum_number_of_events);
void benchmark_compute_flow_destruct(benchmark_compute_flow* benchmark_compute_flow_instance);
void benchmark_compute_flow_handle_packet(benchmark_compute_flow* benchmark_compute_flow_instance, caerEventPacketContainer in, caerEventPacketContainer* out);

struct benchmark_compute_flow_state_struct {
    struct benchmark_compute_flow* benchmark_compute_flow_instance;
};
typedef struct benchmark_compute_flow_state_struct* benchmark_compute_flow_state;

#ifdef __cplusplus
}
#endif
