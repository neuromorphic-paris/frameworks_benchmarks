#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

typedef struct benchmark_reader benchmark_reader;

benchmark_reader* benchmark_reader_construct(char* filename, char* output_filename);
void benchmark_reader_destruct(benchmark_reader* benchmark_reader_instance);
size_t benchmark_reader_number_of_packets(benchmark_reader* benchmark_reader_instance);
size_t benchmark_reader_number_of_events(benchmark_reader* benchmark_reader_instance);
caerEventPacketContainer benchmark_reader_next_packet(benchmark_reader* benchmark_reader_instance);

struct benchmark_reader_state_struct {
    struct benchmark_reader* benchmark_reader_instance;
    bool ended;
};
typedef struct benchmark_reader_state_struct* benchmark_reader_state;

#ifdef __cplusplus
}
#endif
