#include "benchmark.hpp"
#include "../third_party/tarsier/source/convert.hpp"
#include "../third_party/tarsier/source/select_rectangle.hpp"

int main(int argc, char* argv[]) {
    std::vector<sepia::dvs_event> events;
    return benchmark::duration(
        argc,
        argv,
        [&](std::size_t count) {
            events.reserve(count);
        },
        tarsier::make_select_rectangle<sepia::dvs_event>(
            102,
            70,
            100,
            100,
            [&](sepia::dvs_event event) {
                events.push_back(event);
            }),
        [&](uint64_t begin_t, uint64_t end_t) {
            benchmark::events_to_json(std::cout, end_t - begin_t, events);
        });
}
