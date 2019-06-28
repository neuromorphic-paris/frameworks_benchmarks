#pragma once

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/point3d.h>
#include <vector>

struct benchmark_compute_flow {
    public:
    benchmark_compute_flow(
        uint16_t width,
        uint16_t height,
        uint16_t spatial_window,
        uint64_t temporal_window,
        std::size_t minimum_number_of_events);

    /// handle_packet runs the associated algorithm on the given packet.
    void handle_packet(caerEventPacketContainer in, caerEventPacketContainer* out);

    protected:
    /// point represents a point in xyt space.
    struct point {
        float t;
        float x;
        float y;
    };

    const uint16_t _width;
    const uint16_t _height;
    const uint16_t _spatial_window;
    const uint64_t _temporal_window;
    const size_t _minimum_number_of_events;
    std::vector<uint64_t> _ts;
};
