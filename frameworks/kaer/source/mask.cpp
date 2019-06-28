#include "lib_atis.h" // requires libatis
#include "controller.h" // requires kAER
#include "benchmark.hpp"
#include "select_rectangle.hpp"

int main(int argc, char* argv[]) {
    benchmark::check(argc);
    auto controller = new Controller(false);
    auto pipeline_reader = new benchmark::reader(argv[1]);
    controller->add_component(pipeline_reader);
    auto pipeline_select_rectangle = new select_rectangle(pipeline_reader, 102, 70, 100, 100);
    controller->add_component(pipeline_select_rectangle);
    auto pipeline_sink = benchmark::make_sink<Event2d, sepia::dvs_event>(
        pipeline_select_rectangle,
        pipeline_reader->number_of_events(),
        [](Event2d event) {
            return sepia::dvs_event{static_cast<uint64_t>(event.t), event.x, event.y, event.p == 1};
        });
    controller->add_component(pipeline_sink);
    const auto begin_t = benchmark::now();
    for (timestamp t = 0; ; t += 10000) {
        controller->run(10000, t, false);
        if (controller->are_producers_done() && controller->is_pipeline_empty()) {
            break;
        }
    }
    const auto end_t = benchmark::now();
    benchmark::events_to_json(std::cout, end_t - begin_t, pipeline_sink->events());
    return 0;
}
