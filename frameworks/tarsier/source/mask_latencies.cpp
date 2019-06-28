#include "benchmark.hpp"
#include "../third_party/tarsier/source/convert.hpp"
#include "../third_party/tarsier/source/select_rectangle.hpp"

int main(int argc, char* argv[]) {
    std::vector<sepia::dvs_event> events;
    std::vector<std::pair<uint64_t, uint64_t>> points;
    return benchmark::latencies(
        argc,
        argv,
        [&](std::size_t count) {
            events.reserve(count);
            points.reserve(count);
        },
        tarsier::make_select_rectangle<sepia::dvs_event>(
            102,
            70,
            100,
            100,
            [&](sepia::dvs_event event) {
                events.push_back(event);
                points.emplace_back(static_cast<uint64_t>(event.t), benchmark::now());
            }),
        [&](uint64_t time_0) {
            for (auto& point : points) {
                point.second -= time_0;
            }
            benchmark::events_latencies_to_json(std::cout, events, points);
        });
}
