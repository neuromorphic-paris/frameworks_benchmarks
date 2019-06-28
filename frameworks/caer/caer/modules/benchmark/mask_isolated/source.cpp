#include "source.hpp"

benchmark_mask_isolated::benchmark_mask_isolated(uint16_t width, uint16_t height, uint64_t temporal_window) :
    _width(width),
    _height(height),
    _temporal_window(temporal_window),
    _ts(width * height, 0)
{}

void benchmark_mask_isolated::handle_packet(caerEventPacketContainer in) {
    auto packet = reinterpret_cast<caerPolarityEventPacket>(caerEventPacketContainerFindEventPacketByType(in, POLARITY_EVENT));
    if (packet) {
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPolarityEvent event = caerPolarityEventPacketGetEvent(packet, index);
    		if (caerPolarityEventIsValid(event)) {
                const auto t = caerPolarityEventGetTimestamp64(event, packet);
                const auto x = caerPolarityEventGetX(event);
                const auto y = caerPolarityEventGetY(event);
                const auto index = x + y * _width;
                _ts[index] = t + _temporal_window;
                if ((x == 0 || _ts[index - 1] <= t)
                    && (x >= _width - 1 || _ts[index + 1] <= t)
                    && (y == 0 || _ts[index - _width] <= t)
                    && (y >= _height - 1 || _ts[index + _width] <= t)) {
                    caerPolarityEventInvalidate(event, packet);
                }
    		}
        }
    }
}
