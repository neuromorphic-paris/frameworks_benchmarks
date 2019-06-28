#include "source.hpp"

benchmark_reader::benchmark_reader(char* filename, char* output_filename) :
    _event_stream(benchmark::filename_to_event_stream(filename)),
    _output_filename(output_filename),
    _begin_t(0) {
    _next_packet = _event_stream.packets.begin();
}

benchmark_reader::~benchmark_reader() {
    std::ofstream output(_output_filename);
    output << "\"" << _begin_t << "\"";
}

size_t benchmark_reader::number_of_packets() {
    return _event_stream.packets.size();
}

size_t benchmark_reader::number_of_events() {
    return _event_stream.number_of_events;
}

caerEventPacketContainer benchmark_reader::next_packet() {
    if (_next_packet == _event_stream.packets.end()) {
        return NULL;
    }
    if (_next_packet ==  _event_stream.packets.begin()) {
        _begin_t = benchmark::now();
    }
    auto packet = events_to_container(*_next_packet);
    std::advance(_next_packet, 1);
    return packet;
}

caerEventPacketContainer benchmark_reader::events_to_container(const std::vector<sepia::dvs_event>& events) {
    auto container = caerEventPacketContainerAllocate(1);
    auto packet = caerPolarityEventPacketAllocate(static_cast<int32_t>(events.size()), 1, 0);
    caerEventPacketContainerSetEventPacket(container, 0, &(packet->packetHeader));
    container->lowestEventTimestamp = static_cast<int64_t>(events.front().t);
    container->highestEventTimestamp = static_cast<int64_t>(events.back().t);
    container->eventsNumber = static_cast<int32_t>(events.size());
    container->eventsValidNumber = static_cast<int32_t>(events.size());
    packet->packetHeader.eventCapacity = static_cast<int32_t>(events.size());
    packet->packetHeader.eventNumber = 0;
    packet->packetHeader.eventValid = 0;
    for (auto event_iterator = events.begin(); event_iterator != events.end(); ++event_iterator) {
        auto event = caerPolarityEventPacketGetEvent(
            packet,
            static_cast<int32_t>(std::distance(events.begin(), event_iterator))
        );
        caerPolarityEventSetX(event, event_iterator->x);
        caerPolarityEventSetY(event, event_iterator->y);
        caerPolarityEventSetTimestamp(event, static_cast<int32_t>(event_iterator->t));
        caerPolarityEventSetPolarity(event, event_iterator->is_increase);
        caerPolarityEventValidate(event, packet);
    }
    return container;
}
