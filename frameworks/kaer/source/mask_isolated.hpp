#pragma once

#include "combined_filter.h" // requires kAER
#include "timestamp.h" // requires kAER

class mask_isolated : public CombinedFilter {
    public:
        mask_isolated(
            Producer* source,
            uint16_t width,
            uint16_t height,
            uint64_t temporal_window) :
            _source(source),
            _width(width),
            _height(height),
            _temporal_window(temporal_window),
            _ts(width * height, 0) {}
        virtual ~mask_isolated() {}
        void update(timestamp t) override {
            _input_buffer = get_input(_source->get_id(), t);
        }
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            for (unsigned int buffer_index = 0; buffer_index < _input_buffer->size(); ++buffer_index) {
                auto event = *_input_buffer->get_unsafe<Event2d>(buffer_index);
                const auto index = event.x + event.y * _width;
                _ts[index] = event.t + _temporal_window;
                if ((event.x > 0 && _ts[index - 1] > event.t) || (event.x < _width - 1 && _ts[index + 1] > event.t)
                    || (event.y > 0 && _ts[index - _width] > event.t)
                    || (event.y < _height - 1 && _ts[index + _width] > event.t)) {
                    output_buffer->push_back(&event);
                }
            }
        }

     protected:
        Producer* _source;
        EventBuffer* _input_buffer;
        const uint16_t _width;
        const uint16_t _height;
        const uint64_t _temporal_window;
        std::vector<timestamp> _ts;
};
