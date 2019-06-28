#pragma once

#include "combined_filter.h" // requires kAER
#include "timestamp.h" // requires kAER

class compute_flow : public CombinedFilter {
    public:
        compute_flow(
            Producer* source,
            uint16_t width,
            uint16_t height,
            uint16_t spatial_window,
            uint64_t temporal_window,
            std::size_t minimum_number_of_events) :
            _source(source),
            _width(width),
            _height(height),
            _spatial_window(spatial_window),
            _temporal_window(temporal_window),
            _minimum_number_of_events(minimum_number_of_events),
            _ts(width * height, 0) {}
        virtual ~compute_flow() {}
        void update(timestamp t) override {
            _input_buffer = get_input(_source->get_id(), t);
        }
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            for (unsigned int buffer_index = 0; buffer_index < _input_buffer->size(); ++buffer_index) {
                auto event = *_input_buffer->get_unsafe<Event2d>(buffer_index);
                _ts[event.x + event.y * _width] = event.t;
                const auto t_threshold = (event.t <= _temporal_window ? 0 : event.t - _temporal_window);
                std::vector<point> points;
                for (uint16_t y = (event.y <= _spatial_window ? 0 : event.y - _spatial_window);
                     y <= (event.y >= _height - 1 - _spatial_window ? _height - 1 : event.y + _spatial_window);
                     ++y) {
                    for (uint16_t x = (event.x <= _spatial_window ? 0 : event.x - _spatial_window);
                         x <= (event.x >= _width - 1 - _spatial_window ? _width - 1 : event.x + _spatial_window);
                         ++x) {
                        const auto t = _ts[x + y * _width];
                        if (t > t_threshold) {
                            points.push_back(point{
                                static_cast<float>(t),
                                static_cast<float>(x),
                                static_cast<float>(y),
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
                    Event2dVec flow_event(
                        event.x,
                        event.y,
                        t_determinant * x_determinant * inverse_squares_sum,
                        t_determinant * y_determinant * inverse_squares_sum,
                        event.t);
                    output_buffer->push_back(&flow_event);
                }
            }
        }
        void create_buffers(unsigned int ring_size) override {
            for (unsigned int index = 0; index < ring_size; ++index) {
                buffers_.push_back(new Event2dVecBuffer());
            }
        }

    protected:
        /// point represents a point in xyt space.
        struct point {
            float t;
            float x;
            float y;
        };

        Producer* _source;
        EventBuffer* _input_buffer;
        const uint16_t _width;
        const uint16_t _height;
        const uint16_t _spatial_window;
        const uint64_t _temporal_window;
        const std::size_t _minimum_number_of_events;
        std::vector<timestamp> _ts;
};
