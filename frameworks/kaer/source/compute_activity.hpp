#pragma once

#include "combined_filter.h" // requires kAER
#include "timestamp.h" // requires kAER

class compute_activity : public CombinedFilter {
    public:
        compute_activity(Producer* source, uint16_t width, uint16_t height, float decay) :
            _source(source),
            _width(width),
            _decay(decay),
            _potentials_and_ts(width * height, {0.0f, 0}) {}
        virtual ~compute_activity() {}
        void update(timestamp t) override {
            _input_buffer = get_input(_source->get_id(), t);
        }
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            for (unsigned int buffer_index = 0; buffer_index < _input_buffer->size(); ++buffer_index) {
                auto event = *_input_buffer->get_unsafe<Event2dVec>(buffer_index);
                auto& potential_and_t = _potentials_and_ts[event.x + event.y * _width];
                potential_and_t.first =
                    potential_and_t.first * std::exp(-static_cast<float>(event.t - potential_and_t.second) / _decay) + 1;
                potential_and_t.second = event.t;
                Event2dVec activity(
                    event.x,
                    event.y,
                    potential_and_t.first,
                    0.0f,
                    event.t);
                output_buffer->push_back(&activity);
            }
        }
        void create_buffers(unsigned int ring_size) override {
            for (unsigned int index = 0; index < ring_size; ++index) {
                buffers_.push_back(new Event2dVecBuffer());
            }
        }

    protected:
        Producer* _source;
        EventBuffer* _input_buffer;
        const uint16_t _width;
        const float _decay;
        std::vector<std::pair<float, uint64_t>> _potentials_and_ts;
};
