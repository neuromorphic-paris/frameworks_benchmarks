#pragma once

#include "../../../../../../common/benchmark.hpp"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

struct benchmark_reader_latencies {
    public:
    benchmark_reader_latencies(char* filename, char* output_filename);
    ~benchmark_reader_latencies();

    /// number_of_packets returns the number of packets loaded.
    size_t number_of_packets();

    /// number_of_events returns the number of events loaded.
    size_t number_of_events();

    /// next_packet returns the next event packet to push through the pipeline.
    caerEventPacketContainer next_packet();

    protected:
        /// events_to_packet allocates and fills a caer container from a vector of events.
        static caerEventPacketContainer events_to_container(const std::vector<sepia::dvs_event>& events);

        benchmark::event_stream _event_stream;
        std::string _output_filename;
        std::vector<std::vector<sepia::dvs_event>>::iterator _next_packet;
        uint64_t _t_0;
        std::chrono::high_resolution_clock::time_point _time_point_0;
};
