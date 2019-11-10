#pragma once

#include "../../../../../common/benchmark.hpp"
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <iCub/eventdriven/all.h>

namespace benchmark {
    /// check validates the number of program arguments.
    void check(int argc) {
        if (argc != 3) {
            throw std::runtime_error("Syntax: ./run_task /path/to/input.es /path/to/output.json");
        }
    }

    /// network wraps yarp::os::Network calls in throwing functions.
    class network : public yarp::os::Network {
        public:
        network() : yarp::os::Network() {}
        virtual ~network() {}
        void connect(const std::string& source, const std::string& target) {
            if (!yarp::os::Network::connect(source, target, "tcp")) {
                throw std::runtime_error(std::string("connecting '") + source + "' to '" + target + "' failed");
            }
        }
    };

    /// read_port adds an open method to vReadPort.
    template <typename T>
    class read_port : public ev::vReadPort<T> {
        public:
        read_port() : ev::vReadPort<T>() {}
        virtual ~read_port() {}
        bool open(yarp::os::Contact contact) {
            if(!this->port.open(std::move(contact))) {
                return false;
            }
            this->start();
            return true;
        }
    };

    /// write_port adds an open method to vWritePort.
    class write_port : public ev::vWritePort {
        public:
        write_port() : ev::vWritePort() {}
        virtual ~write_port() {}
        bool open(yarp::os::Contact contact) {
            return this->port.open(std::move(contact));
        }
    };

    /// reader wraps file reading in a YARP module.
    class reader : public yarp::os::RFModule {
        public:
        reader(const std::string& filename) :
            yarp::os::RFModule(),
            _event_stream(filename_to_event_stream(filename)),
            _begin_t(0),
            _ready(false) {
            _next_packet = _event_stream.packets.begin();
        }
        virtual double getPeriod() {
            return 1e-6;
        }
        virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
            std::string name = resource_finder.check("name", yarp::os::Value("/reader")).asString();
            yarp::os::RFModule::setName(name.c_str());
            return _output.open(yarp::os::Contact("tcp", "localhost", 20000));
        }
        virtual bool updateModule() override {
            if (_ready.load(std::memory_order_acquire)) {
                Stamp envelope(0, 0.0);
                _begin_t = now();
                for (const auto& packet : _event_stream.packets) {
                    ev::vQueue queue;
                    for (const auto event : packet) {
                        auto address_event = new ev::AddressEvent();
                        address_event->stamp = event.t;
                        address_event->x = event.x;
                        address_event->y = event.y;
                        address_event->polarity = event.is_increase;
                        queue.emplace_back(address_event);
                    }
                    envelope.update();
                    _output.write(queue, envelope);
                }
                return false;
            }
            return true;
        }
        virtual bool close() override {
            _output.close();
            return true;
        }
        virtual uint64_t begin_t() const {
            return _begin_t;
        }
        virtual void ready() {
            _ready.store(true, std::memory_order_release);
        }

        /// number_of_packets returns the number of packets loaded.
        std::size_t number_of_packets() const {
            return _event_stream.packets.size();
        }

        /// number_of_events returns the number of events loaded.
        std::size_t number_of_events() const {
            return _event_stream.number_of_events;
        }

        protected:
        event_stream _event_stream;
        uint64_t _begin_t;
        std::vector<std::vector<sepia::dvs_event>>::iterator _next_packet;
        write_port _output;
        std::atomic_bool _ready;
    };

    /// reader_latencies wraps file reading in a YARP module for the latencies benchmark.
    class reader_latencies : public yarp::os::RFModule {
        public:
        reader_latencies(const std::string& filename) :
            yarp::os::RFModule(),
            _event_stream(filename_to_event_stream(filename)),
            _ready(false) {
            _next_packet = _event_stream.packets.begin();
            _t_0 = _event_stream.packets_ts.front();
        }
        virtual double getPeriod() {
            return 1e-6;
        }
        virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
            std::string name = resource_finder.check("name", yarp::os::Value("/reader_latencies")).asString();
            yarp::os::RFModule::setName(name.c_str());
            return _output.open(yarp::os::Contact("tcp", "localhost", 20000));
        }
        virtual bool updateModule() override {
            if (_ready.load(std::memory_order_acquire)) {
                Stamp envelope(0, 0.0);
                for (std::size_t index = 0; index < _event_stream.packets.size(); ++index) {
                    if (index == 0) {
                        _time_point_0 = std::chrono::high_resolution_clock::now();
                    } else {
                        busy_sleep_until(_time_point_0 + std::chrono::microseconds(_event_stream.packets_ts[index] - _t_0));
                    }
                    ev::vQueue queue;
                    for (const auto event : _event_stream.packets[index]) {
                        auto address_event = new ev::AddressEvent();
                        address_event->stamp = event.t;
                        address_event->x = event.x;
                        address_event->y = event.y;
                        address_event->polarity = event.is_increase;
                        queue.emplace_back(address_event);
                    }
                    envelope.update();
                    _output.write(queue, envelope);
                }
                return false;
            }
            return true;
        }
        virtual bool close() override {
            _output.close();
            return true;
        }
        virtual void ready() {
            _ready.store(true, std::memory_order_release);
        }

        /// number_of_packets returns the number of packets loaded.
        std::size_t number_of_packets() const {
            return _event_stream.packets.size();
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
        std::vector<std::vector<sepia::dvs_event>>::iterator _next_packet;
        uint64_t _t_0;
        std::chrono::high_resolution_clock::time_point _time_point_0;
        write_port _output;
        std::atomic_bool _ready;
    };

    /// sink wraps output checks in a YARP module.
    template <typename YarpEvent, typename Event, typename YarpEventToEvent>
    class sink : public yarp::os::RFModule {
        public:
        sink(std::size_t number_of_packets, std::size_t number_of_events, YarpEventToEvent yarp_event_to_event) :
            yarp::os::RFModule(),
            _number_of_packets(number_of_packets),
            _received_packets(0),
            _end_t(0),
            _yarp_event_to_event(std::forward<YarpEventToEvent>(yarp_event_to_event)) {
            _events.reserve(number_of_events);
        }
        virtual double getPeriod() {
            return 1e-6;
        }
        virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
            std::string name = resource_finder.check("name", yarp::os::Value("/sink")).asString();
            yarp::os::RFModule::setName(name.c_str());
            return _input.open(yarp::os::Contact("tcp", "localhost", 20001));
        }
        virtual bool updateModule() override {
            yarp::os::Stamp stamp;
            auto input_queue = _input.read(stamp);
            for (const auto& generic_event : *input_queue) {
                auto event = ev::is_event<YarpEvent>(generic_event);
                _events.push_back(_yarp_event_to_event(event));
            }
            ++_received_packets;
            if (_received_packets == _number_of_packets) {
                _end_t = now();
                return false;
            }
            return true;
        }
        virtual bool close() override {
            _input.close();
            return true;
        }

        /// events returns the wall clock time measured after receiving the last event.
        virtual uint64_t end_t() const {
            return _end_t;
        }

        /// events returns the output events.
        virtual const std::vector<Event>& events() const {
            return _events;
        }

        protected:
        std::string _output_filename;
        std::size_t _number_of_packets;
        std::size_t _received_packets;
        std::vector<Event> _events;
        uint64_t _end_t;
        read_port<ev::vQueue> _input;
        YarpEventToEvent _yarp_event_to_event;
    };
    template <typename YarpEvent, typename Event, typename YarpEventToEvent>
    std::unique_ptr<sink<YarpEvent, Event, YarpEventToEvent>> make_sink(
        std::size_t number_of_packets, std::size_t number_of_events, YarpEventToEvent yarp_event_to_event) {
        return std::unique_ptr<sink<YarpEvent, Event, YarpEventToEvent>>(
            new sink<YarpEvent, Event, YarpEventToEvent>(
                number_of_packets, number_of_events, std::forward<YarpEventToEvent>(yarp_event_to_event)));
    }

    /// sink_latencies wraps output checks in a YARP module for the latencies benchmark.
    template <typename YarpEvent, typename Event, typename YarpEventToEvent>
    class sink_latencies : public yarp::os::RFModule {
        public:
        sink_latencies(std::size_t number_of_packets, std::size_t number_of_events, YarpEventToEvent yarp_event_to_event) :
            yarp::os::RFModule(),
            _number_of_packets(number_of_packets),
            _received_packets(0),
            _yarp_event_to_event(std::forward<YarpEventToEvent>(yarp_event_to_event)) {
            _events.reserve(number_of_events);
            _points.reserve(number_of_events);
        }
        virtual double getPeriod() {
            return 1e-6;
        }
        virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
            std::string name = resource_finder.check("name", yarp::os::Value("/sink_latencies")).asString();
            yarp::os::RFModule::setName(name.c_str());
            return _input.open(yarp::os::Contact("tcp", "localhost", 20001));
        }
        virtual bool updateModule() override {
            yarp::os::Stamp stamp;
            auto input_queue = _input.read(stamp);
            for (const auto& generic_event : *input_queue) {
                auto event = ev::is_event<YarpEvent>(generic_event);
                _events.push_back(_yarp_event_to_event(event));
                _points.emplace_back(static_cast<uint64_t>(event->stamp), now());
            }
            ++_received_packets;
            return _received_packets < _number_of_packets;
        }
        virtual bool close() override {
            _input.close();
            return true;
        }

        /// events returns the output events.
        virtual const std::vector<Event>& events() const {
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
        std::string _output_filename;
        std::size_t _number_of_packets;
        std::size_t _received_packets;
        std::vector<Event> _events;
        read_port<ev::vQueue> _input;
        YarpEventToEvent _yarp_event_to_event;
        std::vector<std::pair<uint64_t, uint64_t>> _points;
    };
    template <typename YarpEvent, typename Event, typename YarpEventToEvent>
    std::unique_ptr<sink_latencies<YarpEvent, Event, YarpEventToEvent>> make_sink_latencies(
        std::size_t number_of_packets, std::size_t number_of_events, YarpEventToEvent yarp_event_to_event) {
        return std::unique_ptr<sink_latencies<YarpEvent, Event, YarpEventToEvent>>(
            new sink_latencies<YarpEvent, Event, YarpEventToEvent>(
                number_of_packets, number_of_events, std::forward<YarpEventToEvent>(yarp_event_to_event)));
    }
}
