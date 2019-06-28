#include "source.hpp"

benchmark_select_rectangle::benchmark_select_rectangle(uint16_t left, uint16_t bottom, uint16_t width, uint16_t height) :
    _left(left),
    _right(left + width),
    _bottom(bottom),
    _top(bottom + height) {}

void benchmark_select_rectangle::handle_packet(caerEventPacketContainer in) {
    auto packet = reinterpret_cast<caerPolarityEventPacket>(caerEventPacketContainerFindEventPacketByType(in, POLARITY_EVENT));
    if (packet) {
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPolarityEvent event = caerPolarityEventPacketGetEvent(packet, index);
    		if (caerPolarityEventIsValid(event)) {
                const auto x = caerPolarityEventGetX(event);
                const auto y = caerPolarityEventGetY(event);
                if (x < _left || x >= _right || y < _bottom || y >= _top) {
                    caerPolarityEventInvalidate(event, packet);
                }
    		}
        }
    }
}
