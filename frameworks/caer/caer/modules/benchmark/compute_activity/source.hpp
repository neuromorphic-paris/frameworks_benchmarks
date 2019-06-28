#pragma once

#include <libcaer/events/packetContainer.h>
#include <libcaer/events/point2d.h>
#include <libcaer/events/point3d.h>
#include <vector>
#include <cmath>

struct benchmark_compute_activity {
    public:
    benchmark_compute_activity(uint16_t width, uint16_t height, float decay);

    /// handle_packet runs the associated algorithm on the given packet.
    void handle_packet(caerEventPacketContainer in, caerEventPacketContainer* out);

    protected:
    const uint16_t _width;
    const float _decay;
    std::vector<std::pair<float, uint64_t>> _potentials_and_ts;
};
