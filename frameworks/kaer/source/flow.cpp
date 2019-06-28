#include "lib_atis.h" // requires libatis
#include "controller.h" // requires kAER
#include "benchmark.hpp"
#include "split.hpp"
#include "compute_flow.hpp"

int main(int argc, char* argv[]) {
    benchmark::check(argc);
    auto controller = new Controller(false);
    auto pipeline_reader = new benchmark::reader(argv[1]);
    controller->add_component(pipeline_reader);
    auto pipeline_split = new split(pipeline_reader);
    controller->add_component(pipeline_split);
    auto pipeline_compute_flow = new compute_flow(pipeline_split, 304, 240, 3, 1e4, 8);
    controller->add_component(pipeline_compute_flow);
    auto pipeline_sink = benchmark::make_sink<Event2dVec, benchmark::flow>(
        pipeline_compute_flow,
        pipeline_reader->number_of_events(),
        [](Event2dVec event) -> benchmark::flow {
            return {static_cast<uint64_t>(event.t), event.vx_, event.vy_, event.x, event.y};
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
    benchmark::flows_to_json(std::cout, end_t - begin_t, pipeline_sink->events());
    return 0;
}
