#pragma once

#include "../../../../../../common/benchmark.hpp"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/point3d.h>

struct benchmark_flow_sink {
    public:
        benchmark_flow_sink(char* filename, std::size_t number_of_packets, std::size_t number_of_events);
        ~benchmark_flow_sink();

        /// add_packet stores the given packet.
        void add_packet(caerEventPacketContainer container);

    protected:
        std::string _filename;
        std::size_t _number_of_packets;
        std::size_t _received_packets;
        std::vector<benchmark::flow> _flows;
        uint64_t _end_t;
};
