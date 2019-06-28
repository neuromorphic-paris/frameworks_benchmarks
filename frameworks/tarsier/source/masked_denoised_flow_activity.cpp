#include "benchmark.hpp"
#include "../third_party/tarsier/source/compute_activity.hpp"
#include "../third_party/tarsier/source/compute_flow.hpp"
#include "../third_party/tarsier/source/mask_isolated.hpp"
#include "../third_party/tarsier/source/select_rectangle.hpp"

int main(int argc, char* argv[]) {
    std::vector<benchmark::activity> activities;
    return benchmark::duration(
        argc,
        argv,
        [&](std::size_t count) {
            activities.reserve(count);
        },
        sepia::make_split<sepia::type::dvs>(
            tarsier::make_select_rectangle<sepia::simple_event>(
                102,
                70,
                100,
                100,
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
                        tarsier::make_compute_activity<benchmark::flow, benchmark::activity>(
                            304,
                            240,
                            1e5,
                            [](benchmark::flow event, float potential) -> benchmark::activity {
                                return {event.t, potential, event.x, event.y};
                            },
                            [&](benchmark::activity activity) {
                                activities.push_back(activity);
                            })))),
            [](sepia::simple_event) {}),
        [&](uint64_t begin_t, uint64_t end_t) {
            benchmark::activities_to_json(std::cout, end_t - begin_t, activities);
        });
}
