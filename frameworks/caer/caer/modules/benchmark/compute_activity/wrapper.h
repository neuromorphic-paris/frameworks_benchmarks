#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_compute_activity benchmark_compute_activity;

benchmark_compute_activity* benchmark_compute_activity_construct(
    uint16_t width,
    uint16_t height,
    float decay);
void benchmark_compute_activity_destruct(benchmark_compute_activity* benchmark_compute_activity_instance);
void benchmark_compute_activity_handle_packet(benchmark_compute_activity* benchmark_compute_activity_instance, caerEventPacketContainer in, caerEventPacketContainer* out);

struct benchmark_compute_activity_state_struct {
    struct benchmark_compute_activity* benchmark_compute_activity_instance;
};
typedef struct benchmark_compute_activity_state_struct* benchmark_compute_activity_state;

#ifdef __cplusplus
}
#endif
