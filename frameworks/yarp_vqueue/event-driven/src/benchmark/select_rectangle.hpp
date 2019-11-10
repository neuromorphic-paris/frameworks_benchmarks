#pragma once

#include "benchmark.hpp"
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <iCub/eventdriven/all.h>

class select_rectangle : public yarp::os::RFModule {
    public:
    select_rectangle(std::size_t number_of_packets) :
        yarp::os::RFModule(),
        _number_of_packets(number_of_packets),
        _received_packets(0) {}
    virtual ~select_rectangle() {
        _output.close();
        _input.close();
    }
    virtual double getPeriod() {
        return 1e-6;
    }
    virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
        std::string name = resource_finder.check("name", yarp::os::Value("/select_rectangle")).asString();
        yarp::os::RFModule::setName(name.c_str());
        _left = resource_finder.check("left", yarp::os::Value(102)).asInt();
        _bottom = resource_finder.check("bottom", yarp::os::Value(70)).asInt(),
        _right = _left + resource_finder.check("width", yarp::os::Value(100)).asInt();
        _top = _bottom + resource_finder.check("height", yarp::os::Value(100)).asInt();
        return _input.open(yarp::os::Contact("tcp", "localhost", 20002)) && _output.open(yarp::os::Contact("tcp", "localhost", 20003));
    }
    virtual bool updateModule() override {
        yarp::os::Stamp stamp;
        auto input_queue = _input.read(stamp);
        if (input_queue == nullptr) {
            return false;
        }
        ev::vQueue output_queue;
        for (const auto& generic_event : *input_queue) {
            auto event = ev::is_event<ev::AE>(generic_event);
            if (event->x >= _left && event->x < _right && event->y >= _bottom && event->y < _top) {
                output_queue.push_back(event);
            }
        }
        _output.write(output_queue, stamp);
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
    uint16_t _left;
    uint16_t _bottom;
    uint16_t _right;
    uint16_t _top;
    benchmark::read_port<ev::vQueue> _input;
    benchmark::write_port _output;
};
