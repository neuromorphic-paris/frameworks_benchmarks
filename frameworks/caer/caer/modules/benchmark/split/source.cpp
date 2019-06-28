#include "source.hpp"

benchmark_split::benchmark_split() {}

void benchmark_split::handle_packet(caerEventPacketContainer in) {
    auto packet = reinterpret_cast<caerPolarityEventPacket>(caerEventPacketContainerFindEventPacketByType(in, POLARITY_EVENT));
    if (packet) {
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPolarityEvent event = caerPolarityEventPacketGetEvent(packet, index);
    		if (caerPolarityEventIsValid(event)) {
                if (!caerPolarityEventGetPolarity(event)) {
                    caerPolarityEventInvalidate(event, packet);
                }
    		}
        }
    }
}
