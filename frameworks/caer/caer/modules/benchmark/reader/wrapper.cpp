#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_2(benchmark_reader, char*, char*)
BENCHMARK_WRAP_DESTRUCT(benchmark_reader)
BENCHMARK_WRAP(benchmark_reader, std::size_t, number_of_packets, 0)
BENCHMARK_WRAP(benchmark_reader, std::size_t, number_of_events, 0)
BENCHMARK_WRAP(benchmark_reader, caerEventPacketContainer, next_packet, nullptr)
