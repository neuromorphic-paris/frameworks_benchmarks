#pragma once

#include "benchmark.hpp"
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <iCub/eventdriven/all.h>

class compute_activity : public yarp::os::RFModule {
    public:
    compute_activity(std::size_t number_of_packets) :
        yarp::os::RFModule(),
        _number_of_packets(number_of_packets),
        _received_packets(0) {}
    virtual ~compute_activity() {
        _output.close();
        _input.close();
    }
    virtual double getPeriod() {
        return 1e-6;
    }
    virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
        std::string name = resource_finder.check("name", yarp::os::Value("/compute_activity")).asString();
        yarp::os::RFModule::setName(name.c_str());
        _width = resource_finder.check("width", yarp::os::Value(304)).asInt();
        _decay = resource_finder.check("decay", yarp::os::Value(1e5)).asFloat32();
        _potentials_and_ts.resize(_width * resource_finder.check("height", yarp::os::Value(240)).asInt(), {0.0f, 0});
        return _input.open(yarp::os::Contact("tcp", "localhost", 20010)) && _output.open(yarp::os::Contact("tcp", "localhost", 20011));
    }
    virtual bool updateModule() override {
        yarp::os::Stamp stamp;
        auto input_queue = _input.read(stamp);
        if (input_queue == nullptr) {
            return false;
        }
        for (auto generic_event : *input_queue) {
            auto event = ev::is_event<ev::FlowEvent>(generic_event);
            auto& potential_and_t = _potentials_and_ts[event->x + event->y * _width];
            potential_and_t.first =
                potential_and_t.first * std::exp(-static_cast<float>(event->stamp - potential_and_t.second) / _decay) + 1;
            potential_and_t.second = event->stamp;
            event->vx = potential_and_t.first;
            event->vy = 0.0f;
        }
        _output.write(*input_queue, stamp);
        ++_received_packets;
        return _received_packets < _number_of_packets;
    }
    virtual bool close() override {
        _input.close();
        _output.close();
        return true;
    }

    protected:
    std::size_t _number_of_packets;
    std::size_t _received_packets;
    uint16_t _width;
    float _decay;
    std::vector<std::pair<float, uint64_t>> _potentials_and_ts;
    benchmark::read_port<ev::vQueue> _input;
    benchmark::write_port _output;
};
