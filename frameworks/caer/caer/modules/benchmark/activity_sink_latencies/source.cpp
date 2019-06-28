#include "source.hpp"
#include <signal.h>

benchmark_activity_sink_latencies::benchmark_activity_sink_latencies(char* filename, std::size_t number_of_packets, std::size_t number_of_events) :
    _filename(filename),
    _number_of_packets(number_of_packets),
    _received_packets(0) {
    _activities.reserve(number_of_events);
    _points.reserve(number_of_events);
}

benchmark_activity_sink_latencies::~benchmark_activity_sink_latencies() {
    std::ofstream output(_filename);
    benchmark::activities_latencies_to_json(output, _activities, _points);
}

void benchmark_activity_sink_latencies::add_packet(caerEventPacketContainer container) {
    if (container) {
        auto packet = reinterpret_cast<caerPoint2DEventPacket>(caerEventPacketContainerFindEventPacketByType(container, POINT2D_EVENT));
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPoint2DEventConst event = caerPoint2DEventPacketGetEventConst(packet, index);
    		if (caerPoint2DEventIsValid(event)) {
                const float xy = caerPoint2DEventGetY(event);
                _activities.push_back({
                    static_cast<uint64_t>(caerPoint2DEventGetTimestamp64(event, packet)),
                    caerPoint2DEventGetX(event),
                    *reinterpret_cast<const uint16_t*>(&xy),
                    *(reinterpret_cast<const uint16_t*>(&xy) + 1),
                });
                _points.emplace_back(static_cast<uint64_t>(_activities.back().t), benchmark::now());
    		}
        }
    }
    ++_received_packets;
    if (_received_packets == _number_of_packets) {
        raise(SIGINT);
    }
}
