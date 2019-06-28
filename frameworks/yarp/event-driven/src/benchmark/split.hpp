#pragma once

#include "benchmark.hpp"
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <iCub/eventdriven/all.h>

class split : public yarp::os::RFModule {
    public:
    split(std::size_t number_of_packets) :
        yarp::os::RFModule(),
        _number_of_packets(number_of_packets),
        _received_packets(0) {}
    virtual ~split() {
        _output.close();
        _input.close();
    }
    virtual double getPeriod() {
        return 1e-6;
    }
    virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
        std::string name = resource_finder.check("name", yarp::os::Value("/split")).asString();
        yarp::os::RFModule::setName(name.c_str());
        return _input.open(yarp::os::Contact("tcp", "localhost", 20004)) && _output.open(yarp::os::Contact("tcp", "localhost", 20005));
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
            if (event->polarity == 1) {
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
    benchmark::read_port<ev::vQueue> _input;
    benchmark::write_port _output;
};
