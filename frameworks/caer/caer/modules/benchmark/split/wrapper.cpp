#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_0(benchmark_split)
BENCHMARK_WRAP_DESTRUCT(benchmark_split)
BENCHMARK_WRAP_VOID_1(benchmark_split, handle_packet, caerEventPacketContainer)
