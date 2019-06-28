#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_4(benchmark_select_rectangle, uint16_t, uint16_t, uint16_t, uint16_t)
BENCHMARK_WRAP_DESTRUCT(benchmark_select_rectangle)
BENCHMARK_WRAP_VOID_1(benchmark_select_rectangle, handle_packet, caerEventPacketContainer)
