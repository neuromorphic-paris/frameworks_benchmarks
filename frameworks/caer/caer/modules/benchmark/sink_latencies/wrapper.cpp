#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_3(benchmark_sink_latencies, char*, std::size_t, std::size_t)
BENCHMARK_WRAP_DESTRUCT(benchmark_sink_latencies)
BENCHMARK_WRAP_VOID_1(benchmark_sink_latencies, add_packet, caerEventPacketContainer)
