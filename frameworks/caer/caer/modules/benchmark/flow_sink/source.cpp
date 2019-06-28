#include "source.hpp"
#include <signal.h>

benchmark_flow_sink::benchmark_flow_sink(char* filename, std::size_t number_of_packets, std::size_t number_of_events) :
    _filename(filename),
    _number_of_packets(number_of_packets),
    _received_packets(0),
    _end_t(0) {
    _flows.reserve(number_of_events);
}

benchmark_flow_sink::~benchmark_flow_sink() {
    std::ofstream output(_filename);
    benchmark::flows_to_json(output, _end_t, _flows);
}

void benchmark_flow_sink::add_packet(caerEventPacketContainer container) {
    if (container) {
        auto packet = reinterpret_cast<caerPoint3DEventPacket>(caerEventPacketContainerFindEventPacketByType(container, POINT3D_EVENT));
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPoint3DEventConst event = caerPoint3DEventPacketGetEventConst(packet, index);
    		if (caerPoint3DEventIsValid(event)) {
                const float xy = caerPoint3DEventGetZ(event);
                _flows.push_back({
                    static_cast<uint64_t>(caerPoint3DEventGetTimestamp64(event, packet)),
                    caerPoint3DEventGetX(event),
                    caerPoint3DEventGetY(event),
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
