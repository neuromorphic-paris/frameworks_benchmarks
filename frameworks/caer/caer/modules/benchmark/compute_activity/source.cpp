#include "source.hpp"

benchmark_compute_activity::benchmark_compute_activity(
    uint16_t width,
    uint16_t height,
    float decay) :
    _width(width),
    _decay(decay),
    _potentials_and_ts(width * height, {0.0f, 0}) {}

void benchmark_compute_activity::handle_packet(caerEventPacketContainer in, caerEventPacketContainer* out) {
    auto packet = reinterpret_cast<caerPoint3DEventPacket>(caerEventPacketContainerFindEventPacketByType(in, POINT3D_EVENT));
    if (packet && packet->packetHeader.eventValid) {
        *out = caerEventPacketContainerAllocate(1);
        auto out_packet = caerPoint2DEventPacketAllocate(packet->packetHeader.eventValid, 7, 0);
        caerEventPacketContainerSetEventPacket(*out, 0, &(out_packet->packetHeader));
        (*out)->eventsNumber = packet->packetHeader.eventValid;
        out_packet->packetHeader.eventCapacity = packet->packetHeader.eventValid;
        out_packet->packetHeader.eventNumber = 0;
        out_packet->packetHeader.eventValid = 0;
        int32_t out_index = 0;
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPoint3DEvent event = caerPoint3DEventPacketGetEvent(packet, index);
    		if (caerPoint3DEventIsValid(event)) {
                const uint64_t t = caerPoint3DEventGetTimestamp64(event, packet);
                const float xy = caerPoint3DEventGetZ(event);
                const uint16_t x = *reinterpret_cast<const uint16_t*>(&xy);
                const uint16_t y = *(reinterpret_cast<const uint16_t*>(&xy) + 1);
                auto& potential_and_t = _potentials_and_ts[x + y * _width];
                potential_and_t.first = potential_and_t.first * std::exp(-static_cast<float>(t - potential_and_t.second) / _decay) + 1;
                potential_and_t.second = t;
                if (out_index == 0) {
                    (*out)->lowestEventTimestamp = static_cast<int64_t>(t);
                }
                (*out)->highestEventTimestamp = static_cast<int64_t>(t);
                caerPoint2DEvent out_event = caerPoint2DEventPacketGetEvent(out_packet, out_index);
                ++out_index;
                caerPoint2DEventSetTimestamp(out_event, static_cast<int32_t>(t));
                caerPoint2DEventSetX(out_event, potential_and_t.first);
                caerPoint2DEventSetY(out_event, xy);
                caerPoint2DEventValidate(out_event, out_packet);
    		}
        }
        (*out)->eventsValidNumber = out_index;
    }
}
