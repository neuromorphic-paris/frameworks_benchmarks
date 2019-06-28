#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_reader_latencies benchmark_reader_latencies;

benchmark_reader_latencies* benchmark_reader_latencies_construct(char* filename, char* output_filename);
void benchmark_reader_latencies_destruct(benchmark_reader_latencies* benchmark_reader_latencies_instance);
size_t benchmark_reader_latencies_number_of_packets(benchmark_reader_latencies* benchmark_reader_latencies_instance);
size_t benchmark_reader_latencies_number_of_events(benchmark_reader_latencies* benchmark_reader_latencies_instance);
caerEventPacketContainer benchmark_reader_latencies_next_packet(benchmark_reader_latencies* benchmark_reader_latencies_instance);

struct benchmark_reader_latencies_state_struct {
    struct benchmark_reader_latencies* benchmark_reader_latencies_instance;
    bool ended;
};
typedef struct benchmark_reader_latencies_state_struct* benchmark_reader_latencies_state;

#ifdef __cplusplus
}
#endif
