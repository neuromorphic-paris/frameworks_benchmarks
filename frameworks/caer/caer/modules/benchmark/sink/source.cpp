#include "source.hpp"
#include <signal.h>

benchmark_sink::benchmark_sink(char* filename, std::size_t number_of_packets, std::size_t number_of_events) :
    _filename(filename),
    _number_of_packets(number_of_packets),
    _received_packets(0),
    _end_t(0) {
    _events.reserve(number_of_events);
}

benchmark_sink::~benchmark_sink() {
    std::ofstream output(_filename);
    benchmark::events_to_json(output, _end_t, _events);
}

void benchmark_sink::add_packet(caerEventPacketContainer container) {
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
    		}
        }
    }
    ++_received_packets;
    if (_received_packets == _number_of_packets) {
        _end_t = benchmark::now();
        raise(SIGINT);
    }
}
