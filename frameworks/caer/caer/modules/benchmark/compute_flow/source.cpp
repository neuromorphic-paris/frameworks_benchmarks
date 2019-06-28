#include "source.hpp"

benchmark_compute_flow::benchmark_compute_flow(
    uint16_t width,
    uint16_t height,
    uint16_t spatial_window,
    uint64_t temporal_window,
    std::size_t minimum_number_of_events) :
    _width(width),
    _height(height),
    _spatial_window(spatial_window),
    _temporal_window(temporal_window),
    _minimum_number_of_events(minimum_number_of_events),
    _ts(width * height, 0) {}

void benchmark_compute_flow::handle_packet(caerEventPacketContainer in, caerEventPacketContainer* out) {
    auto packet = reinterpret_cast<caerPolarityEventPacket>(caerEventPacketContainerFindEventPacketByType(in, POLARITY_EVENT));
    if (packet && packet->packetHeader.eventValid) {
        *out = caerEventPacketContainerAllocate(1);
        auto out_packet = caerPoint3DEventPacketAllocate(packet->packetHeader.eventValid, 3, 0);
        caerEventPacketContainerSetEventPacket(*out, 0, &(out_packet->packetHeader));
        (*out)->eventsNumber = packet->packetHeader.eventValid;
        out_packet->packetHeader.eventCapacity = packet->packetHeader.eventValid;
        out_packet->packetHeader.eventNumber = 0;
        out_packet->packetHeader.eventValid = 0;
        int32_t out_index = 0;
        for (int32_t index = 0; index < caerEventPacketHeaderGetEventNumber(&(packet->packetHeader)); ++index) {
    		caerPolarityEvent event = caerPolarityEventPacketGetEvent(packet, index);
    		if (caerPolarityEventIsValid(event)) {
                const uint64_t t = caerPolarityEventGetTimestamp64(event, packet);
                const uint16_t x = caerPolarityEventGetX(event);
                const uint16_t y = caerPolarityEventGetY(event);
                _ts[x + y * _width] = t;
                const uint64_t t_threshold = (t <= _temporal_window ? 0 : t - _temporal_window);
                std::vector<point> points;
                for (uint16_t y_other = (y <= _spatial_window ? 0 : y - _spatial_window);
                     y_other <= (y >= _height - 1 - _spatial_window ? _height - 1 : y + _spatial_window);
                     ++y_other) {
                    for (uint16_t x_other = (x <= _spatial_window ? 0 : x - _spatial_window);
                         x_other <= (x >= _width - 1 - _spatial_window ? _width - 1 : x + _spatial_window);
                         ++x_other) {
                        const auto t_other = _ts[x_other + y_other * _width];
                        if (t_other > t_threshold) {
                            points.push_back(point{
                                static_cast<float>(t_other),
                                static_cast<float>(x_other),
                                static_cast<float>(y_other),
                            });
                        }
                    }
                }
                if (points.size() >= _minimum_number_of_events) {
                    auto t_mean = 0.0f;
                    auto x_mean = 0.0f;
                    auto y_mean = 0.0f;
                    for (auto point : points) {
                        t_mean += point.t;
                        x_mean += point.x;
                        y_mean += point.y;
                    }
                    t_mean /= points.size();
                    x_mean /= points.size();
                    y_mean /= points.size();
                    auto tx_sum = 0.0f;
                    auto ty_sum = 0.0f;
                    auto xx_sum = 0.0f;
                    auto xy_sum = 0.0f;
                    auto yy_sum = 0.0f;
                    for (auto point : points) {
                        const auto t_delta = point.t - t_mean;
                        const auto x_delta = point.x - x_mean;
                        const auto y_delta = point.y - y_mean;
                        tx_sum += t_delta * x_delta;
                        ty_sum += t_delta * y_delta;
                        xx_sum += x_delta * x_delta;
                        xy_sum += x_delta * y_delta;
                        yy_sum += y_delta * y_delta;
                    }
                    const auto t_determinant = xx_sum * yy_sum - xy_sum * xy_sum;
                    const auto x_determinant = tx_sum * yy_sum - ty_sum * xy_sum;
                    const auto y_determinant = ty_sum * xx_sum - tx_sum * xy_sum;
                    const auto inverse_squares_sum = 1.0f / (x_determinant * x_determinant + y_determinant * y_determinant);
                    if (out_index == 0) {
                        (*out)->lowestEventTimestamp = static_cast<int64_t>(t);
                    }
                    (*out)->highestEventTimestamp = static_cast<int64_t>(t);
                    caerPoint3DEvent out_event = caerPoint3DEventPacketGetEvent(out_packet, out_index);
                    ++out_index;
                    caerPoint3DEventSetTimestamp(out_event, static_cast<int32_t>(t));
                    float xy;
                    *reinterpret_cast<uint16_t*>(&xy) = x;
                    *(reinterpret_cast<uint16_t*>(&xy) + 1) = y;
                    caerPoint3DEventSetX(out_event, t_determinant * x_determinant * inverse_squares_sum);
                    caerPoint3DEventSetY(out_event, t_determinant * y_determinant * inverse_squares_sum);
                    caerPoint3DEventSetZ(out_event, xy);
                    caerPoint3DEventValidate(out_event, out_packet);

                    //std::cout << "    " << t << ", " << x << ", " << y << std::endl; // @DEBUG

                }
    		}
        }
        (*out)->eventsValidNumber = out_index;
    }
}
