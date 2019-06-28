#include "benchmark.hpp"
#include "compute_flow.hpp"
#include "mask_isolated.hpp"
#include "select_rectangle.hpp"
#include "split.hpp"

int main(int argc, char* argv[]) {
    benchmark::check(argc);
    benchmark::network network;
    yarp::os::ResourceFinder resource_finder;
    benchmark::reader_latencies reader_module(argv[1]);
    split split_module(reader_module.number_of_packets());
    select_rectangle select_rectangle_module(reader_module.number_of_packets());
    mask_isolated mask_isolated_module(reader_module.number_of_packets());
    compute_flow compute_flow_module(reader_module.number_of_packets());
    auto sink_module = benchmark::make_sink_latencies<ev::FlowEvent, benchmark::flow>(
        reader_module.number_of_packets(),
        reader_module.number_of_events(),
        [](const ev::event<ev::FlowEvent>& event) -> benchmark::flow {
            return {
                static_cast<uint64_t>(event->stamp),
                event->vx,
                event->vy,
                static_cast<uint16_t>(event->x),
                static_cast<uint16_t>(event->y)};
        });
    reader_module.configure(resource_finder);
    split_module.configure(resource_finder);
    select_rectangle_module.configure(resource_finder);
    mask_isolated_module.configure(resource_finder);
    compute_flow_module.configure(resource_finder);
    sink_module->configure(resource_finder);
    network.connect("/localhost:20000", "/localhost:20004");
    network.connect("/localhost:20005", "/localhost:20002");
    network.connect("/localhost:20003", "/localhost:20006");
    network.connect("/localhost:20007", "/localhost:20008");
    network.connect("/localhost:20009", "/localhost:20001");
    reader_module.runModuleThreaded();
    split_module.runModuleThreaded();
    select_rectangle_module.runModuleThreaded();
    mask_isolated_module.runModuleThreaded();
    compute_flow_module.runModuleThreaded();
    sink_module->runModuleThreaded();
    reader_module.ready();
    try {
        sink_module->joinModule(120);
        compute_flow_module.joinModule(120);
        mask_isolated_module.joinModule(120);
        select_rectangle_module.joinModule(120);
        split_module.joinModule(120);
        reader_module.joinModule(120);
    } catch (...) {}
    std::ofstream output(argv[2]);
    benchmark::flows_latencies_to_json(
        output,
        sink_module->events(),
        sink_module->points(reader_module.time_0()));
    return 0;
}
