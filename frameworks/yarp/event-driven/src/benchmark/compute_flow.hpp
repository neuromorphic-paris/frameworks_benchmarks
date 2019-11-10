#pragma once

#include "benchmark.hpp"
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <iCub/eventdriven/all.h>

class compute_flow : public yarp::os::RFModule {
    public:
    compute_flow(std::size_t number_of_packets) :
        yarp::os::RFModule(),
        _number_of_packets(number_of_packets),
        _received_packets(0) {}
    virtual ~compute_flow() {
        _output.close();
        _input.close();
    }
    virtual double getPeriod() {
        return 1e-6;
    }
    virtual bool configure(yarp::os::ResourceFinder& resource_finder) override {
        std::string name = resource_finder.check("name", yarp::os::Value("/compute_flow")).asString();
        yarp::os::RFModule::setName(name.c_str());
        _width = resource_finder.check("width", yarp::os::Value(304)).asInt();
        _height = resource_finder.check("height", yarp::os::Value(240)).asInt(),
        _spatial_window = resource_finder.check("spatial_window", yarp::os::Value(3)).asInt();
        _temporal_window = resource_finder.check("temporal_window", yarp::os::Value(1e4)).asInt();
        _minimum_number_of_events = resource_finder.check("minimum_number_of_events", yarp::os::Value(8)).asInt();
        _ts.resize(_width * _height, 0);
        return _input.open(yarp::os::Contact("tcp", "localhost", 20008)) && _output.open(yarp::os::Contact("tcp", "localhost", 20009));
    }
    virtual bool updateModule() override {
        yarp::os::Stamp stamp;
        auto input_queue = _input.read(stamp);
        if (input_queue == nullptr) {
            return false;
        }
        std::deque<ev::FlowEvent> output_queue;
        for (const auto& event : *input_queue) {
            _ts[event.x + event.y * _width] = event.stamp;
            const auto t_threshold = (event.stamp <= _temporal_window ? 0 : event.stamp - _temporal_window);
            std::vector<point> points;
            for (uint16_t y = (event.y <= _spatial_window ? 0 : event.y - _spatial_window);
                 y <= (event.y >= _height - 1 - _spatial_window ? _height - 1 : event.y + _spatial_window);
                 ++y) {
                for (uint16_t x = (event.x <= _spatial_window ? 0 : event.x - _spatial_window);
                     x <= (event.x >= _width - 1 - _spatial_window ? _width - 1 : event.x + _spatial_window);
                     ++x) {
                    const auto t = _ts[x + y * _width];
                    if (t > t_threshold) {
                        points.push_back(point{
                            static_cast<float>(t),
                            static_cast<float>(x),
                            static_cast<float>(y),
                        });
                    }
                }
            }
            if (points.size() >= _minimum_number_of_events) {
                auto t_mean = 0.0f;
                auto x_mean = 0.0f;
                auto y_mean = 0.0f;
                for (auto point : points) {
                    t_mean += point.t;
                    x_mean += point.x;
                    y_mean += point.y;
                }
                t_mean /= points.size();
                x_mean /= points.size();
                y_mean /= points.size();
                auto tx_sum = 0.0f;
                auto ty_sum = 0.0f;
                auto xx_sum = 0.0f;
                auto xy_sum = 0.0f;
                auto yy_sum = 0.0f;
                for (auto point : points) {
                    const auto t_delta = point.t - t_mean;
                    const auto x_delta = point.x - x_mean;
                    const auto y_delta = point.y - y_mean;
                    tx_sum += t_delta * x_delta;
                    ty_sum += t_delta * y_delta;
                    xx_sum += x_delta * x_delta;
                    xy_sum += x_delta * y_delta;
                    yy_sum += y_delta * y_delta;
                }
                const auto t_determinant = xx_sum * yy_sum - xy_sum * xy_sum;
                const auto x_determinant = tx_sum * yy_sum - ty_sum * xy_sum;
                const auto y_determinant = ty_sum * xx_sum - tx_sum * xy_sum;
                const auto inverse_squares_sum = 1.0f / (x_determinant * x_determinant + y_determinant * y_determinant);
                ev::FlowEvent flow_event(event);
                flow_event.vx = t_determinant * x_determinant * inverse_squares_sum;
                flow_event.vy = t_determinant * y_determinant * inverse_squares_sum;
                output_queue.push_back(flow_event);

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
    /// point represents a point in xyt space.
    struct point {
        float t;
        float x;
        float y;
    };

    std::size_t _number_of_packets;
    std::size_t _received_packets;
    uint16_t _width;
    uint16_t _height;
    uint16_t _spatial_window;
    uint64_t _temporal_window;
    std::size_t _minimum_number_of_events;
    std::vector<uint64_t> _ts;
    benchmark::read_port<std::vector<ev::AddressEvent>> _input;
    benchmark::write_port _output;
};
