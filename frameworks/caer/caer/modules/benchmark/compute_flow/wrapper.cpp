#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_5(benchmark_compute_flow, uint16_t, uint16_t, uint16_t, uint64_t, size_t)
BENCHMARK_WRAP_DESTRUCT(benchmark_compute_flow)
BENCHMARK_WRAP_VOID_2(benchmark_compute_flow, handle_packet, caerEventPacketContainer, caerEventPacketContainer*)
