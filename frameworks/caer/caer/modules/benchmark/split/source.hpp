#pragma once

#include "../../../../../../common/benchmark.hpp"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

struct benchmark_split {
    public:
    benchmark_split();

    /// handle_packet runs the associated algorithm on the given packet.
    void handle_packet(caerEventPacketContainer in);
};
