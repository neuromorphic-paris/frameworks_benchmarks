#pragma once

#include "../../../common/benchmark.hpp"
#include "../../../common/third_party/pontella/source/pontella.hpp"

namespace benchmark {
    /// duration wraps a pipeline for a duration benchmark.
    template <typename HandleCount, typename HandleEvent, typename HandleTs>
    int duration(int argc, char* argv[], HandleCount handle_count, HandleEvent handle_event, HandleTs handle_ts) {
        return pontella::main(
            {
                "duration measures the duration of an algorithm for the given Event Stream file",
                "Syntax: ./duration /path/to/input.es"
            },
            argc,
            argv,
            1,
            {},
            {}, [&](pontella::command command) {
                const auto input_event_stream = filename_to_event_stream(command.arguments.front());
                handle_count(input_event_stream.number_of_events);
                const auto begin_t = now();
                for (const auto& packet : input_event_stream.packets) {
                    for (const auto event : packet) {
                        handle_event(event);
                    }
                }
                const auto end_t = now();
                handle_ts(begin_t, end_t);
            });
    }

    /// latencies wraps a pipeline for a latencies benchmark.
    template <typename HandleCount, typename HandleEvent, typename HandleTs>
    int latencies(
        int argc,
        char* argv[],
        HandleCount handle_count,
        HandleEvent handle_event,
        HandleTs handle_ts) {
        return pontella::main(
            {
                "latencies measures the delay between data availability and algorithm output for the given Event Stream file",
                "Syntax: ./latencies /path/to/input.es"
            },
            argc,
            argv,
            1,
            {},
            {}, [&](pontella::command command) {
                const auto input_event_stream = filename_to_event_stream(command.arguments.front());
                handle_count(input_event_stream.number_of_events);
                const auto t_0 = input_event_stream.packets_ts.front();
                std::chrono::high_resolution_clock::time_point time_point_0;
                for (std::size_t index = 0; index < input_event_stream.packets.size(); ++index) {
                    if (index == 0) {
                        time_point_0 = std::chrono::high_resolution_clock::now();
                    } else {
                        busy_sleep_until(time_point_0 + std::chrono::microseconds(input_event_stream.packets_ts[index] - t_0));
                    }
                    for (const auto event : input_event_stream.packets[index]) {
                        handle_event(event);
                    }
                }
                handle_ts(time_point_to_uint64(time_point_0));
            });
    }
}
