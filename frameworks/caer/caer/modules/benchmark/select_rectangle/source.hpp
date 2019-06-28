#pragma once

#include "../../../../../../common/benchmark.hpp"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>

struct benchmark_select_rectangle {
    public:
    benchmark_select_rectangle(uint16_t left, uint16_t bottom, uint16_t width, uint16_t height);

    /// handle_packet runs the associated algorithm on the given packet.
    void handle_packet(caerEventPacketContainer in);

    protected:
    const uint16_t _left;
    const uint16_t _right;
    const uint16_t _bottom;
    const uint16_t _top;
};
