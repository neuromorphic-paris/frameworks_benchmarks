#pragma once

#include "combined_filter.h" // requires kAER
#include "timestamp.h" // requires kAER

class select_rectangle : public CombinedFilter {
    public:
        select_rectangle(Producer* source, uint16_t left, uint16_t bottom, uint16_t width, uint16_t height) :
            _source(source),
            _left(left),
            _bottom(bottom),
            _right(left + width),
            _top(bottom + height) {}
        virtual ~select_rectangle() {}
        void update(timestamp t) override {
            _input_buffer = get_input(_source->get_id(), t);
        }
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            for (unsigned int buffer_index = 0; buffer_index < _input_buffer->size(); ++buffer_index) {
                auto event = *_input_buffer->get_unsafe<Event2d>(buffer_index);
                if (event.x >= _left && event.x < _right && event.y >= _bottom && event.y < _top) {
                    output_buffer->push_back(&event);
                }
            }
        }

     protected:
        Producer* _source;
        EventBuffer* _input_buffer;
        const uint16_t _left;
        const uint16_t _bottom;
        const uint16_t _right;
        const uint16_t _top;
};
