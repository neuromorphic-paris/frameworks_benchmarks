#include  "benchmark.hpp"
#include "third_party/pontella/source/pontella.hpp"

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "packetize returns a JSON array containing the last event timestamp in each packet",
            "packets contain up to 5000 events, and last up to 10000 us",
            "Syntax: ./packetize /path/to/input.es",
        },
        argc,
        argv,
        1,
        {},
        {}, [&](pontella::command command) {
            const auto event_stream = benchmark::filename_to_event_stream(command.arguments.front());
            std::cout << "[";
            for (std::size_t index = 0; index < event_stream.packets_ts.size(); ++index) {
                if (index > 0) {
                    std::cout << ",";
                }
                std::cout << event_stream.packets_ts[index];
            }
            std::cout << "]";
        });
}
