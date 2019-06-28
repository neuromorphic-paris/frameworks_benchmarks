#include "lib_atis.h" // requires libatis
#include "controller.h" // requires kAER
#include "benchmark.hpp"
#include "select_rectangle.hpp"

int main(int argc, char* argv[]) {
    benchmark::check(argc);
    auto controller = new Controller(false);
    auto pipeline_reader_latencies = new benchmark::reader_latencies(argv[1]);
    controller->add_component(pipeline_reader_latencies);
    auto pipeline_select_rectangle = new select_rectangle(pipeline_reader_latencies, 102, 70, 100, 100);
    controller->add_component(pipeline_select_rectangle);
    auto pipeline_sink_latencies = benchmark::make_sink_latencies<Event2d, sepia::dvs_event>(
        pipeline_select_rectangle,
        pipeline_reader_latencies->number_of_events(),
        [](Event2d event) {
            return sepia::dvs_event{static_cast<uint64_t>(event.t), event.x, event.y, event.p == 1};
        });
    controller->add_component(pipeline_sink_latencies);
    for (timestamp t = 0; ; t += 10000) {
        controller->run(10000, t, false);
        if (controller->are_producers_done() && controller->is_pipeline_empty()) {
            break;
        }
    }
    const auto& events = pipeline_sink_latencies->events();
    benchmark::events_latencies_to_json(
        std::cout,
        pipeline_sink_latencies->events(),
        pipeline_sink_latencies->points(pipeline_reader_latencies->time_0()));
    return 0;
}
