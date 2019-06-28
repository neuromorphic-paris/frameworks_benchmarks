#include "source.hpp"
#include <signal.h>

benchmark_activity_sink::benchmark_activity_sink(char* filename, std::size_t number_of_packets, std::size_t number_of_events) :
    _filename(filename),
    _number_of_packets(number_of_packets),
    _received_packets(0),
    _end_t(0) {
    _activities.reserve(number_of_events);
}

benchmark_activity_sink::~benchmark_activity_sink() {
    std::ofstream output(_filename);
    benchmark::activities_to_json(output, _end_t, _activities);
}

void benchmark_activity_sink::add_packet(caerEventPacketContainer container) {
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
    		}
        }
    }
    ++_received_packets;
    if (_received_packets == _number_of_packets) {
        _end_t = benchmark::now();
        raise(SIGINT);
    }
}
