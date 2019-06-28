#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_3(benchmark_mask_isolated, uint16_t, uint16_t, uint64_t)
BENCHMARK_WRAP_DESTRUCT(benchmark_mask_isolated)
BENCHMARK_WRAP_VOID_1(benchmark_mask_isolated, handle_packet, caerEventPacketContainer)
