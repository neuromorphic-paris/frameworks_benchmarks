#pragma once

#include "benchmark.hpp"
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <iCub/eventdriven/all.h>

class mask_isolated : public yarp::os::RFModule {
    public:
    mask_isolated(std::size_t number_of_packets) :
        yarp::os::RFModule(),
        _number_of_packets(number_of_packets),
        _received_packets(0) {}
    virtual ~mask_isolated() {
        _output.close();
        _input.close();
    }
    virtual double getPeriod() {
        return 1e-6;
    }
    virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
        std::string name = resource_finder.check("name", yarp::os::Value("/mask_isolated")).asString();
        yarp::os::RFModule::setName(name.c_str());
        _width = resource_finder.check("width", yarp::os::Value(304)).asInt();
        _height = resource_finder.check("height", yarp::os::Value(240)).asInt(),
        _temporal_window = resource_finder.check("temporal_window", yarp::os::Value(1e3)).asInt();
        _ts.resize(_width * _height, 0);
        return _input.open(yarp::os::Contact("tcp", "localhost", 20006)) && _output.open(yarp::os::Contact("tcp", "localhost", 20007));
    }
    virtual bool updateModule() override {
        yarp::os::Stamp stamp;
        auto input_queue = _input.read(stamp);
        if (input_queue == nullptr) {
            return false;
        }
        std::deque<ev::AddressEvent> output_queue;
        for (const auto& event : *input_queue) {
            const auto index = event.x + event.y * _width;
            _ts[index] = event.stamp + _temporal_window;
            if ((event.x > 0 && _ts[index - 1] > event.stamp) || (event.x < _width - 1 && _ts[index + 1] > event.stamp)
                || (event.y > 0 && _ts[index - _width] > event.stamp)
                || (event.y < _height - 1 && _ts[index + _width] > event.stamp)) {
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
    uint16_t _width;
    uint16_t _height;
    uint64_t _temporal_window;
    std::vector<uint64_t> _ts;
    benchmark::read_port<std::vector<ev::AddressEvent>> _input;
    benchmark::write_port _output;
};
