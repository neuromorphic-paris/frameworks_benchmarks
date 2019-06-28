#pragma once

#include "../../../../../../common/benchmark.hpp"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/point3d.h>

struct benchmark_flow_sink_latencies {
    public:
        benchmark_flow_sink_latencies(char* filename, std::size_t number_of_packets, std::size_t number_of_events);
        ~benchmark_flow_sink_latencies();

        /// add_packet stores the given packet.
        void add_packet(caerEventPacketContainer container);

    protected:
        std::string _filename;
        std::size_t _number_of_packets;
        std::size_t _received_packets;
        std::vector<benchmark::flow> _flows;
        std::vector<std::pair<uint64_t, uint64_t>> _points;
};
