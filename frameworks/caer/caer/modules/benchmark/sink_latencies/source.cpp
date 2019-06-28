#include "source.hpp"
#include <signal.h>

benchmark_sink_latencies::benchmark_sink_latencies(char* filename, std::size_t number_of_packets, std::size_t number_of_events) :
    _filename(filename),
    _number_of_packets(number_of_packets),
    _received_packets(0)  {
    _events.reserve(number_of_events);
    _points.reserve(number_of_events);
}

benchmark_sink_latencies::~benchmark_sink_latencies() {
    std::ofstream output(_filename);
    benchmark::events_latencies_to_json(output, _events, _points);
}

void benchmark_sink_latencies::add_packet(caerEventPacketContainer container) {
    if (container) {
        auto packet = reinterpret_cast<caerPolarityEventPacket>(caerEventPacketContainerFindEventPacketByType(container, POLARITY_EVENT));
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPolarityEventConst event = caerPolarityEventPacketGetEventConst(packet, index);
    		if (caerPolarityEventIsValid(event)) {
                _events.push_back({
                    static_cast<uint64_t>(caerPolarityEventGetTimestamp64(event, packet)),
                    caerPolarityEventGetX(event),
                    caerPolarityEventGetY(event),
                    caerPolarityEventGetPolarity(event),
                });
                _points.emplace_back(static_cast<uint64_t>(_events.back().t), benchmark::now());
    		}
        }
    }
    ++_received_packets;
    if (_received_packets == _number_of_packets) {
        raise(SIGINT);
    }
}
