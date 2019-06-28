#pragma once

#include "third_party/sepia/source/sepia.hpp"
#include "third_party/tarsier/source/hash.hpp"
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace benchmark {
    ///  time_point_to_uint64 converts a time point to an integer timestamp (in ns).
    inline uint64_t time_point_to_uint64(std::chrono::high_resolution_clock::time_point time_point) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(time_point.time_since_epoch()).count());
    }

    /// now returns the current wall clock time as an integer (in ns).
    inline uint64_t now() {
        return time_point_to_uint64(std::chrono::high_resolution_clock::now());
    }

    /// busy_sleep_until implements a for-loop based sleep_until function.
    inline void busy_sleep_until(std::chrono::high_resolution_clock::time_point time_point) {
        while (std::chrono::high_resolution_clock::now() < time_point) {}
    }

    /// flow is the output type of the flow pipelines.
    SEPIA_PACK(struct flow {
        uint64_t t;
        float vx;
        float vy;
        uint16_t x;
        uint16_t y;
    });

    /// activity is the output type of the masked_denoised_flow_activity pipeline.
    SEPIA_PACK(struct activity {
        uint64_t t;
        float potential;
        uint16_t x;
        uint16_t y;
    });

    /// event_stream contains loaded event packets and pre-calculated timestamps.
    struct event_stream {
        /// each packet contains up to 5000 events, with up to 10000 us between the first and the last.
        std::vector<std::vector<sepia::dvs_event>> packets;

        /// number_of_events is the total number of events.
        std::size_t number_of_events;

        /// packets_ts contains each packet's last event timestamp.
        std::vector<uint64_t> packets_ts;
    };

    /// hash_events calculates the MurmurHash3 (128 bits, x64 version).
    template <typename Uint, typename EventIterator, typename EventToUint>
    std::string hash_events(EventIterator begin, EventIterator end, EventToUint event_to_uint) {
        std::string result;
        {
            auto hash = tarsier::make_hash<uint64_t>([&](std::pair<uint64_t, uint64_t> hash_value) {
                std::stringstream stream;
                stream << '"' << std::hex << std::get<1>(hash_value) << std::hex << std::setfill('0') << std::setw(16)
                       << std::get<0>(hash_value) << '"';
                result = stream.str();
            });
            for (; begin != end; ++begin) {
                hash(event_to_uint(*begin));
            }
        }
        return result;
    }

    /// filename_to_event_stream returns packets and pre-calculated timestamps.
    inline event_stream filename_to_event_stream(const std::string& filename) {
        event_stream result{{}, 0, {}};
        result.packets.emplace_back();
        sepia::join_observable<sepia::type::dvs>(
            sepia::filename_to_ifstream(filename),
            [&](sepia::dvs_event event) {
                ++result.number_of_events;
                auto& events = result.packets.back();
                if (events.empty()) {
                    events.push_back(event);
                } else {
                    if (events.size() >= 5000 || event.t >= events.front().t + 10000) {
                        result.packets_ts.push_back(events.back().t);
                        result.packets.push_back(std::vector<sepia::dvs_event>{event});
                    } else {
                        events.push_back(event);
                    }
                }
            });
        result.packets_ts.push_back(result.packets.back().back().t);
        return result;
    }

    /// events_to_json writes the given vector of events to the output.
    /// t is a timestamp or the elapsed time, depending on available information.
    inline void events_to_json(std::ostream& output, uint64_t t, const std::vector<sepia::dvs_event>& events) {
        output
            << "["
            << t << ","
            << events.size() << ","
            << std::count_if(events.begin(), events.end(), [](sepia::dvs_event event) { return event.is_increase; }) << ","
            << hash_events<uint64_t>(events.begin(), events.end(), [](sepia::dvs_event event) { return event.t; }) << ","
            << hash_events<uint16_t>(events.begin(), events.end(), [](sepia::dvs_event event) { return event.x; }) << ","
            << hash_events<uint16_t>(events.begin(), events.end(), [](sepia::dvs_event event) { return event.y; }) << "]";
    }

    /// flows_to_json writes the given vector of flow events to the output.
    /// t is a timestamp or the elapsed time, depending on available information.
    inline void flows_to_json(std::ostream& output, uint64_t t, const std::vector<flow>& flows) {
        output
            << "["
            << t << ","
            << flows.size() << ","
            << hash_events<uint64_t>(flows.begin(), flows.end(), [](flow event) { return event.t; }) << ","
            << hash_events<uint32_t>(flows.begin(), flows.end(), [](flow event) {
                const auto v = event.vx;
                return *reinterpret_cast<const uint32_t*>(&v);
            }) << ","
            << hash_events<uint32_t>(flows.begin(), flows.end(), [](flow event) {
                const auto v = event.vy;
                return *reinterpret_cast<const uint32_t*>(&v);
            }) << ","
            << hash_events<uint16_t>(flows.begin(), flows.end(), [](flow event) { return event.x; }) << ","
            << hash_events<uint16_t>(flows.begin(), flows.end(), [](flow event) { return event.y; }) << "]";
    }

    /// activities_to_json writes the given vector of activity events to the output.
    /// time is the wall clock time or the elapsed time (depending on available information) in ns.
    inline void activities_to_json(std::ostream& output, uint64_t time, const std::vector<activity>& activities) {
        output
            << "["
            << time << ","
            << activities.size() << ","
            << hash_events<uint64_t>(activities.begin(), activities.end(), [](activity event) { return event.t; }) << ","
            << hash_events<uint32_t>(activities.begin(), activities.end(), [](activity event) {
                const auto potential = event.potential;
                return *reinterpret_cast<const uint32_t*>(&potential);
            }) << ","
            << hash_events<uint16_t>(activities.begin(), activities.end(), [](activity event) { return event.x; }) << ","
            << hash_events<uint16_t>(activities.begin(), activities.end(), [](activity event) { return event.y; }) << "]";
    }

    /// events_latencies_to_json writes the given vector of events and latencies to the output.
    /// points is a vector of pairs [t, time], where t is the event timestamp in us,
    /// and time is the wall clock time or the elapsed time (depending on available information) in ns.
    inline void events_latencies_to_json(
        std::ostream& output,
        const std::vector<sepia::dvs_event>& events,
        const std::vector<std::pair<uint64_t, uint64_t>>& points) {
        output
            << "["
            << events.size() << ","
            << std::count_if(events.begin(), events.end(), [](sepia::dvs_event event) { return event.is_increase; }) << ","
            << hash_events<uint64_t>(events.begin(), events.end(), [](sepia::dvs_event event) { return event.t; }) << ","
            << hash_events<uint16_t>(events.begin(), events.end(), [](sepia::dvs_event event) { return event.x; }) << ","
            << hash_events<uint16_t>(events.begin(), events.end(), [](sepia::dvs_event event) { return event.y; }) << ",[";
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (index > 0) {
                output << ",";
            }
            output << "[" << points[index].first << ",\"" << points[index].second << "\"]";
        }
        output << "]]";
    }

    /// flows_latencies_to_json writes the given vector of flow events and latencies to the output.
    /// points is a vector of pairs [t, time], where t is the event timestamp in us,
    /// and time is the wall clock time or the elapsed time (depending on available information) in ns.
    inline void flows_latencies_to_json(
        std::ostream& output,
        const std::vector<flow>& flows,
        const std::vector<std::pair<uint64_t, uint64_t>>& points) {
        output
            << "["
            << flows.size() << ","
            << hash_events<uint64_t>(flows.begin(), flows.end(), [](flow event) { return event.t; }) << ","
            << hash_events<uint32_t>(flows.begin(), flows.end(), [](flow event) {
                const auto v = event.vx;
                return *reinterpret_cast<const uint32_t*>(&v);
            }) << ","
            << hash_events<uint32_t>(flows.begin(), flows.end(), [](flow event) {
                const auto v = event.vy;
                return *reinterpret_cast<const uint32_t*>(&v);
            }) << ","
            << hash_events<uint16_t>(flows.begin(), flows.end(), [](flow event) { return event.x; }) << ","
            << hash_events<uint16_t>(flows.begin(), flows.end(), [](flow event) { return event.y; }) << ",[";
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (index > 0) {
                output << ",";
            }
            output << "[" << points[index].first << ",\"" << points[index].second << "\"]";
        }
        output << "]]";
    }

    /// activities_latencies_to_json writes the given vector of activity events and latencies to the output.
    /// points is a vector of pairs [t, time], where t is the event timestamp in us,
    /// and time is the wall clock time or the elapsed time (depending on available information) in ns.
    inline void activities_latencies_to_json(
        std::ostream& output,
        const std::vector<activity>& activities,
        const std::vector<std::pair<uint64_t, uint64_t>>& points) {
        output
            << "["
            << activities.size() << ","
            << hash_events<uint64_t>(activities.begin(), activities.end(), [](activity event) { return event.t; }) << ","
            << hash_events<uint32_t>(activities.begin(), activities.end(), [](activity event) {
                const auto potential = event.potential;
                return *reinterpret_cast<const uint32_t*>(&potential);
            }) << ","
            << hash_events<uint16_t>(activities.begin(), activities.end(), [](activity event) { return event.x; }) << ","
            << hash_events<uint16_t>(activities.begin(), activities.end(), [](activity event) { return event.y; }) << ",[";
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (index > 0) {
                output << ",";
            }
            output << "[" << points[index].first << ",\"" << points[index].second << "\"]";
        }
        output << "]]";
    }
}
