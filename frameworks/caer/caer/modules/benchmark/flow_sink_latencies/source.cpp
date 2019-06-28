#include "source.hpp"
#include <signal.h>

benchmark_flow_sink_latencies::benchmark_flow_sink_latencies(char* filename, std::size_t number_of_packets, std::size_t number_of_events) :
    _filename(filename),
    _number_of_packets(number_of_packets),
    _received_packets(0) {
    _flows.reserve(number_of_events);
    _points.reserve(number_of_events);
}

benchmark_flow_sink_latencies::~benchmark_flow_sink_latencies() {
    std::ofstream output(_filename);
    benchmark::flows_latencies_to_json(output, _flows, _points);
}

void benchmark_flow_sink_latencies::add_packet(caerEventPacketContainer container) {
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
                _points.emplace_back(static_cast<uint64_t>(_flows.back().t), benchmark::now());
    		}
        }
    }
    ++_received_packets;
    if (_received_packets == _number_of_packets) {
        raise(SIGINT);
    }
}
