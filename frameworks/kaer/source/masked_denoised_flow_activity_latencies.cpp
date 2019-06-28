#include "lib_atis.h" // requires libatis
#include "controller.h" // requires kAER
#include "benchmark.hpp"
#include "split.hpp"
#include "compute_flow.hpp"
#include "mask_isolated.hpp"
#include "select_rectangle.hpp"
#include "compute_activity.hpp"

int main(int argc, char* argv[]) {
    benchmark::check(argc);
    auto controller = new Controller(false);
    auto pipeline_reader_latencies = new benchmark::reader_latencies(argv[1]);
    controller->add_component(pipeline_reader_latencies);
    auto pipeline_split = new split(pipeline_reader_latencies);
    controller->add_component(pipeline_split);
    auto pipeline_select_rectangle = new select_rectangle(pipeline_split, 102, 70, 100, 100);
    controller->add_component(pipeline_select_rectangle);
    auto pipeline_mask_isolated = new mask_isolated(pipeline_select_rectangle, 304, 240, 1e3);
    controller->add_component(pipeline_mask_isolated);
    auto pipeline_compute_flow = new compute_flow(pipeline_mask_isolated, 304, 240, 3, 1e4, 8);
    controller->add_component(pipeline_compute_flow);
    auto pipeline_compute_activity = new compute_activity(pipeline_compute_flow, 304, 240, 1e5);
    controller->add_component(pipeline_compute_activity);
    auto pipeline_sink_latencies = benchmark::make_sink_latencies<Event2dVec, benchmark::activity>(
        pipeline_compute_activity,
        pipeline_reader_latencies->number_of_events(),
        [](Event2dVec event) -> benchmark::activity {
            return {static_cast<uint64_t>(event.t), event.vx_, event.x, event.y};
        });
    controller->add_component(pipeline_sink_latencies);
    for (timestamp t = 0; ; t += 10000) {
        controller->run(10000, t, false);
        if (controller->are_producers_done() && controller->is_pipeline_empty()) {
            break;
        }
    }
    benchmark::activities_latencies_to_json(
        std::cout,
        pipeline_sink_latencies->events(),
        pipeline_sink_latencies->points(pipeline_reader_latencies->time_0()));
    return 0;
}
