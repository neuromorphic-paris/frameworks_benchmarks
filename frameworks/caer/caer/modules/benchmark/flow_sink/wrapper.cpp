#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_3(benchmark_flow_sink, char*, std::size_t, std::size_t)
BENCHMARK_WRAP_DESTRUCT(benchmark_flow_sink)
BENCHMARK_WRAP_VOID_1(benchmark_flow_sink, add_packet, caerEventPacketContainer)
