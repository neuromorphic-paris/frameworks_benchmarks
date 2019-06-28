#include "../utilities.h"
#include "source.hpp"
#include "wrapper.h"

BENCHMARK_WRAP_CONSTRUCT_2(benchmark_reader_latencies, char*, char*)
BENCHMARK_WRAP_DESTRUCT(benchmark_reader_latencies)
BENCHMARK_WRAP(benchmark_reader_latencies, std::size_t, number_of_packets, 0)
BENCHMARK_WRAP(benchmark_reader_latencies, std::size_t, number_of_events, 0)
BENCHMARK_WRAP(benchmark_reader_latencies, caerEventPacketContainer, next_packet, nullptr)
