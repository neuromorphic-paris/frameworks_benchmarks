#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_split benchmark_split;

benchmark_split* benchmark_split_construct();
void benchmark_split_destruct(benchmark_split* benchmark_split_instance);
void benchmark_split_handle_packet(benchmark_split* benchmark_split_instance, caerEventPacketContainer in);

struct benchmark_split_state_struct {
    struct benchmark_split* benchmark_split_instance;
};
typedef struct benchmark_split_state_struct* benchmark_split_state;

#ifdef __cplusplus
}
#endif
