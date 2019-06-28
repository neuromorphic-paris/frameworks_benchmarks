#pragma once

#include "producer.h" // requires kAER
#include "consumer.h" // requires kAER
#include "../../../common/benchmark.hpp"
#include <iostream>

namespace benchmark {
    /// check validates the number of program arguments.
    void check(int argc) {
        if (argc != 2) {
            throw std::runtime_error("Syntax: ./run_task /path/to/input.es");
        }
    }

    /// reader wraps file reading in a kAER producer.
    class reader : public Producer {
        public:
        reader(const std::string& filename) :
            _event_stream(filename_to_event_stream(filename)),
            _is_done(false) {
            _packet_iterator = _event_stream.packets.begin();
            _event_iterator = _packet_iterator->begin();
        }
        virtual ~reader() {}
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            while (_packet_iterator != _event_stream.packets.end()) {
                for (; _event_iterator != _packet_iterator->end(); ++_event_iterator) {
                    if (_event_iterator->t >= t) {
                        break;
                    }
                    Event2d event(_event_iterator->x, _event_iterator->y, _event_iterator->is_increase, _event_iterator->t);
                    output_buffer->push_back(&event);
                }
                if (_event_iterator != _packet_iterator->end()) {
                    break;
                }
                ++_packet_iterator;
                if (_packet_iterator == _event_stream.packets.end()) {
                    _is_done = true;
                    break;
                }
                _event_iterator = _packet_iterator->begin();
            }
        }
        bool is_done() override {
            return _is_done;
        }

        /// number_of_events returns the number of events loaded.
        std::size_t number_of_events() const {
            return _event_stream.number_of_events;
        }

        protected:
        event_stream _event_stream;
        bool _is_done;
        std::vector<std::vector<sepia::dvs_event>>::iterator _packet_iterator;
        std::vector<sepia::dvs_event>::iterator _event_iterator;
    };

    /// reader_latencies wraps file reading in a kAER producer for the latencies benchmark.
    class reader_latencies : public Producer {
        public:
        reader_latencies(const std::string& filename) :
            _event_stream(filename_to_event_stream(filename)),
            _is_done(false) {
            _packet_iterator = _event_stream.packets.begin();
            _event_iterator = _packet_iterator->begin();
            _t_0 = _event_stream.packets_ts.front();
        }
        virtual ~reader_latencies() {}
        void update_output(timestamp t, int buffer_id, bool analog_output_needed) override {
            if (_packet_iterator == _event_stream.packets.begin() && _event_iterator == _packet_iterator->begin()) {
                _time_point_0 = std::chrono::high_resolution_clock::now();
            }
            auto output_buffer = buffers_[buffer_id];
            output_buffer->clear();
            while (_packet_iterator != _event_stream.packets.end()) {
                for (; _event_iterator != _packet_iterator->end(); ++_event_iterator) {
                    if (_event_iterator->t >= t) {
                        break;
                    }
                    Event2d event(_event_iterator->x, _event_iterator->y, _event_iterator->is_increase, _event_iterator->t);
                    output_buffer->push_back(&event);
                }
                if (_event_iterator != _packet_iterator->end()) {
                    break;
                }
                ++_packet_iterator;
                busy_sleep_until(_time_point_0 + std::chrono::microseconds(
                    _event_stream.packets_ts[std::distance(_event_stream.packets.begin(), _packet_iterator)] - _t_0));
                if (_packet_iterator == _event_stream.packets.end()) {
                    _is_done = true;
                    break;
                }
                _event_iterator = _packet_iterator->begin();
            }
        }
        bool is_done() override {
            return _is_done;
        }

        /// number_of_events returns the number of events loaded.
        std::size_t number_of_events() const {
            return _event_stream.number_of_events;
        }

        /// time_0 returns the wall clock time read when the first packet was dispatched.
        uint64_t time_0() const {
            return time_point_to_uint64(_time_point_0);
        }

        protected:
        event_stream _event_stream;
        bool _is_done;
        std::vector<std::vector<sepia::dvs_event>>::iterator _packet_iterator;
        std::vector<sepia::dvs_event>::iterator _event_iterator;
        uint64_t _t_0;
        std::chrono::high_resolution_clock::time_point _time_point_0;
    };

    /// sink wraps output checks in a kAER producer.
    template <typename KaerEvent, typename Event, typename KaerEventToEvent>
    class sink : public Consumer {
        public:
        sink(Producer* source, std::size_t number_of_events, KaerEventToEvent kaer_event_to_event) :
            _source(source),
            _kaer_event_to_event(std::forward<KaerEventToEvent>(kaer_event_to_event)) {
            _events.reserve(number_of_events);
        }
        virtual ~sink() {}
        void update(timestamp t) {
            auto buffer = get_input(_source->get_id(), t);
            for (unsigned int index = 0; index < buffer->size(); ++index) {
                _events.push_back(_kaer_event_to_event(*buffer->template get_unsafe<KaerEvent>(index)));
            }
        }

        /// events returns the output events.
        const std::vector<Event>& events() const {
            return _events;
        }

        protected:
        Producer* _source;
        std::vector<Event> _events;
        KaerEventToEvent _kaer_event_to_event;
    };
    template <typename KaerEvent, typename Event, typename KaerEventToEvent>
    sink<KaerEvent, Event, KaerEventToEvent>* make_sink(Producer* source, std::size_t number_of_events, KaerEventToEvent kaer_event_to_event) {
        return new sink<KaerEvent, Event, KaerEventToEvent>(source, number_of_events, std::forward<KaerEventToEvent>(kaer_event_to_event));
    }

    /// sink_latencies wraps output checks in a kAER producer for the latencies benchmark.
    template <typename KaerEvent, typename Event, typename KaerEventToEvent>
    class sink_latencies : public Consumer {
        public:
        sink_latencies(Producer* source, std::size_t number_of_events, KaerEventToEvent kaer_event_to_event) :
            _source(source),
            _kaer_event_to_event(std::forward<KaerEventToEvent>(kaer_event_to_event)) {
            _events.reserve(number_of_events);
            _points.reserve(number_of_events);
        }
        virtual ~sink_latencies() {}
        void update(timestamp t) {
            auto buffer = get_input(_source->get_id(), t);
            for (unsigned int index = 0; index < buffer->size(); ++index) {
                const auto event = *buffer->template get_unsafe<KaerEvent>(index);
                _events.push_back(_kaer_event_to_event(event));
                _points.emplace_back(static_cast<uint64_t>(event.t), now());
            }
        }

        /// events returns the output events.
        const std::vector<Event>& events() const {
            return _events;
        }

        /// points returns the measured wall clock times.
        /// time_0 (wall clock time in nanoseconds) is used to normalize wall clock times.
        std::vector<std::pair<uint64_t, uint64_t>> points(uint64_t time_0) const {
            std::vector<std::pair<uint64_t, uint64_t>> result(_points.size());
            std::transform(_points.begin(), _points.end(), result.begin(), [&](std::pair<uint64_t, uint64_t> point) {
                point.second -= time_0;
                return point;
            });
            return result;
        }

        protected:
        Producer* _source;
        std::vector<Event> _events;
        KaerEventToEvent _kaer_event_to_event;
        std::vector<std::pair<uint64_t, uint64_t>> _points;
    };
    template <typename KaerEvent, typename Event, typename KaerEventToEvent>
    sink_latencies<KaerEvent, Event, KaerEventToEvent>* make_sink_latencies(Producer* source, std::size_t number_of_events, KaerEventToEvent kaer_event_to_event) {
        return new sink_latencies<KaerEvent, Event, KaerEventToEvent>(source, number_of_events, std::forward<KaerEventToEvent>(kaer_event_to_event));
    }
}
