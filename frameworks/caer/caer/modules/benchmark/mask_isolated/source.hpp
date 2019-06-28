#pragma once

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>
#include <vector>

struct benchmark_mask_isolated {
    public:
    benchmark_mask_isolated(uint16_t width, uint16_t height, uint64_t temporal_window);

    /// handle_packet runs the associated algorithm on the given packet.
    void handle_packet(caerEventPacketContainer in);

    protected:
    const uint16_t _width;
    const uint16_t _height;
    const uint64_t _temporal_window;
    std::vector<uint64_t> _ts;
};
