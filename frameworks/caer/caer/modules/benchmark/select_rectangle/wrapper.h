#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_select_rectangle benchmark_select_rectangle;

benchmark_select_rectangle* benchmark_select_rectangle_construct(uint16_t left, uint16_t bottom, uint16_t width, uint16_t height);
void benchmark_select_rectangle_destruct(benchmark_select_rectangle* benchmark_select_rectangle_instance);
void benchmark_select_rectangle_handle_packet(benchmark_select_rectangle* benchmark_select_rectangle_instance, caerEventPacketContainer in);

struct benchmark_select_rectangle_state_struct {
    struct benchmark_select_rectangle* benchmark_select_rectangle_instance;
};
typedef struct benchmark_select_rectangle_state_struct* benchmark_select_rectangle_state;

#ifdef __cplusplus
}
#endif
