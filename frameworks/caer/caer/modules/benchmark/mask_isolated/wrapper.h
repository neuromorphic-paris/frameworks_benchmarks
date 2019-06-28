#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_mask_isolated benchmark_mask_isolated;

benchmark_mask_isolated* benchmark_mask_isolated_construct(uint16_t width, uint16_t height, uint64_t temporal_window);
void benchmark_mask_isolated_destruct(benchmark_mask_isolated* benchmark_mask_isolated_instance);
void benchmark_mask_isolated_handle_packet(benchmark_mask_isolated* benchmark_mask_isolated_instance, caerEventPacketContainer in);

struct benchmark_mask_isolated_state_struct {
    struct benchmark_mask_isolated* benchmark_mask_isolated_instance;
};
typedef struct benchmark_mask_isolated_state_struct* benchmark_mask_isolated_state;

#ifdef __cplusplus
}
#endif
