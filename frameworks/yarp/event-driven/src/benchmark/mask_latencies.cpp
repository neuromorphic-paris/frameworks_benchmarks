#include "benchmark.hpp"
#include "select_rectangle.hpp"

int main(int argc, char* argv[]) {
    benchmark::check(argc);
    benchmark::network network;
    yarp::os::ResourceFinder resource_finder;
    benchmark::reader_latencies reader_module(argv[1]);
    select_rectangle select_rectangle_module(reader_module.number_of_packets());
    auto sink_module = benchmark::make_sink_latencies<ev::AE, sepia::dvs_event>(
        reader_module.number_of_packets(),
        reader_module.number_of_events(),
        [](const ev::event<ev::AE>& event) -> sepia::dvs_event {
            return {static_cast<uint64_t>(event->stamp), static_cast<uint16_t>(event->x), static_cast<uint16_t>(event->y), event->polarity};
        });
    reader_module.configure(resource_finder);
    select_rectangle_module.configure(resource_finder);
    sink_module->configure(resource_finder);
    network.connect("/localhost:20000", "/localhost:20002");
    network.connect("/localhost:20003", "/localhost:20001");
    reader_module.runModuleThreaded();
    select_rectangle_module.runModuleThreaded();
    sink_module->runModuleThreaded();
    reader_module.ready();
    try {
        sink_module->joinModule(120);
        select_rectangle_module.joinModule(120);
        reader_module.joinModule(120);
    } catch (...) {}
    std::ofstream output(argv[2]);
    benchmark::events_latencies_to_json(
        output,
        sink_module->events(),
        sink_module->points(reader_module.time_0()));
    return 0;
}
