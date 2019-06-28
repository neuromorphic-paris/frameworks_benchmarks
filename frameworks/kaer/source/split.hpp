#pragma once

#include "combined_filter.h" // requires kAER
#include "timestamp.h" // requires kAER

class split : public CombinedFilter {
    public:
        split(Producer* source) : _source(source) {}
        virtual ~split() {}
        void update(timestamp t) override {
            _input_buffer = get_input(_source->get_id(), t);
        }
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            for (unsigned int buffer_index = 0; buffer_index < _input_buffer->size(); ++buffer_index) {
                auto event = *_input_buffer->get_unsafe<Event2d>(buffer_index);
                if (event.p == 1) {
                    output_buffer->push_back(&event);
                }
            }
        }

     protected:
        Producer* _source;
        EventBuffer* _input_buffer;
};
