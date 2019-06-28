#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_3(benchmark_compute_activity, uint16_t, uint16_t, float)
BENCHMARK_WRAP_DESTRUCT(benchmark_compute_activity)
BENCHMARK_WRAP_VOID_2(benchmark_compute_activity, handle_packet, caerEventPacketContainer, caerEventPacketContainer*)
