#include "benchmark.hpp"
#include "../third_party/tarsier/source/compute_flow.hpp"
#include "../third_party/tarsier/source/mask_isolated.hpp"

int main(int argc, char* argv[]) {
    std::vector<benchmark::flow> flows;
    std::vector<std::pair<uint64_t, uint64_t>> points;
    return benchmark::latencies(
        argc,
        argv,
        [&](std::size_t count) {
            flows.reserve(count);
            points.reserve(count);
        },
        sepia::make_split<sepia::type::dvs>(
            tarsier::make_mask_isolated<sepia::simple_event>(
                304,
                240,
                1e3,
                tarsier::make_compute_flow<sepia::simple_event, benchmark::flow>(
                    304,
                    240,
                    3,
                    1e4,
                    8,
                    [](sepia::simple_event event, float vx, float vy) -> benchmark::flow {
                        return {event.t, vx, vy, event.x, event.y};
                    },
                    [&](benchmark::flow flow) {
                        flows.push_back(flow);
                        points.emplace_back(static_cast<uint64_t>(flow.t), benchmark::now());
                    })),
            [](sepia::simple_event) {}),
        [&](uint64_t time_0) {
            for (auto& point : points) {
                point.second -= time_0;
            }
            benchmark::flows_latencies_to_json(std::cout, flows, points);
        });
}
