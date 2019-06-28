#include "benchmark.hpp"
#include "../third_party/tarsier/source/compute_flow.hpp"
#include "../third_party/tarsier/source/mask_isolated.hpp"

int main(int argc, char* argv[]) {
    std::vector<benchmark::flow> flows;
    return benchmark::duration(
        argc,
        argv,
        [&](std::size_t count) {
            flows.reserve(count);
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
                    })),
            [](sepia::simple_event) {}),
        [&](uint64_t begin_t, uint64_t end_t) {
            benchmark::flows_to_json(std::cout, end_t - begin_t, flows);
        });
}
