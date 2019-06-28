#pragma once

#include "../../../../../../common/benchmark.hpp"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

struct benchmark_sink {
    public:
        benchmark_sink(char* filename, std::size_t number_of_packets, std::size_t number_of_events);
        ~benchmark_sink();

        /// add_packet stores the given packet.
        void add_packet(caerEventPacketContainer container);

    protected:
        std::string _filename;
        std::size_t _number_of_packets;
        std::size_t _received_packets;
        std::vector<sepia::dvs_event> _events;
        uint64_t _end_t;
};
