#define CATCH_CONFIG_MAIN
#include "../source/sepia.hpp"
#include "../third_party/Catch2/single_include/catch.hpp"
#include <sstream>

const std::string examples = sepia::join({sepia::dirname(SEPIA_DIRNAME), "third_party", "event_stream", "examples"});

TEST_CASE("read generic header type", "[sepia::read_type]") {
    REQUIRE(
        sepia::read_header(sepia::filename_to_ifstream(sepia::join({examples, "generic.es"}))).event_stream_type
        == sepia::type::generic);
}

TEST_CASE("read DVS header type", "[sepia::read_type]") {
    REQUIRE(
        sepia::read_header(sepia::filename_to_ifstream(sepia::join({examples, "dvs.es"}))).event_stream_type
        == sepia::type::dvs);
}

TEST_CASE("read ATIS header type", "[sepia::read_type]") {
    REQUIRE(
        sepia::read_header(sepia::filename_to_ifstream(sepia::join({examples, "atis.es"}))).event_stream_type
        == sepia::type::atis);
}

TEST_CASE("read color header type", "[sepia::read_type]") {
    REQUIRE(
        sepia::read_header(sepia::filename_to_ifstream(sepia::join({examples, "color.es"}))).event_stream_type
        == sepia::type::color);
}

TEST_CASE("count generic events", "[sepia::join_observable<sepia::type::generic>]") {
    std::size_t count = 0;
    sepia::join_observable<sepia::type::generic>(
        sepia::filename_to_ifstream(sepia::join({examples, "generic.es"})),
        [&](sepia::generic_event) -> void { ++count; });
    if (count != 70) {
        FAIL(
            "the observable generated an unexpected number of events (expected 70, got " + std::to_string(count) + ")");
    }
}

TEST_CASE("count DVS events", "[sepia::join_observable<sepia::type::atis>]") {
    std::size_t count = 0;
    sepia::join_observable<sepia::type::dvs>(
        sepia::filename_to_ifstream(sepia::join({examples, "dvs.es"})), [&](sepia::dvs_event) -> void { ++count; });
    if (count != 473225) {
        FAIL(
            "the observable generated an unexpected number of events (expected 473225, got " + std::to_string(count)
            + ")");
    }
}

TEST_CASE("count ATIS events", "[sepia::join_observable<sepia::type::atis>]") {
    std::size_t count = 0;
    sepia::join_observable<sepia::type::atis>(
        sepia::filename_to_ifstream(sepia::join({examples, "atis.es"})), [&](sepia::atis_event) -> void { ++count; });
    if (count != 1326017) {
        FAIL(
            "the observable generated an unexpected number of events (expected 1326017, got " + std::to_string(count)
            + ")");
    }
}

TEST_CASE("count color events", "[sepia::join_observable<sepia::type::color>]") {
    std::size_t count = 0;
    sepia::join_observable<sepia::type::color>(
        sepia::filename_to_ifstream(sepia::join({examples, "color.es"})), [&](sepia::color_event) -> void { ++count; });
    if (count != 473225) {
        FAIL(
            "the observable generated an unexpected number of events (expected 473225, got " + std::to_string(count)
            + ")");
    }
}

TEST_CASE("write generic events", "[sepia::write<sepia::type::generic>]") {
    const auto filename = sepia::join({examples, "generic.es"});
    std::string bytes;
    {
        auto input = sepia::filename_to_ifstream(filename);
        input->seekg(0, std::ifstream::end);
        bytes.resize(static_cast<std::size_t>(input->tellg()));
        input->seekg(0, std::ifstream::beg);
        input->read(&bytes[0], bytes.size());
    }
    std::string output_bytes;
    {
        std::ostringstream output;
        sepia::join_observable<sepia::type::generic>(
            sepia::filename_to_ifstream(filename), sepia::write_to_reference<sepia::type::generic>(output));
        output_bytes = output.str();
    }
    REQUIRE(bytes.size() == output_bytes.size());
    REQUIRE(std::strcmp(bytes.c_str(), output_bytes.c_str()) == 0);
}

TEST_CASE("write DVS events", "[sepia::write<sepia::type::dvs>]") {
    const auto filename = sepia::join({examples, "dvs.es"});
    std::string bytes;
    {
        auto input = sepia::filename_to_ifstream(filename);
        input->seekg(0, std::ifstream::end);
        bytes.resize(static_cast<std::size_t>(input->tellg()));
        input->seekg(0, std::ifstream::beg);
        input->read(&bytes[0], bytes.size());
    }
    std::string output_bytes;
    {
        const auto header = sepia::read_header(sepia::filename_to_ifstream(filename));
        std::ostringstream output;
        sepia::join_observable<sepia::type::dvs>(
            sepia::filename_to_ifstream(filename),
            sepia::write_to_reference<sepia::type::dvs>(output, header.width, header.height));
        output_bytes = output.str();
    }
    REQUIRE(bytes.size() == output_bytes.size());
    REQUIRE(std::strcmp(bytes.c_str(), output_bytes.c_str()) == 0);
}

TEST_CASE("write ATIS events", "[sepia::write<sepia::type::atis>]") {
    const auto filename = sepia::join({examples, "atis.es"});
    std::string bytes;
    {
        auto input = sepia::filename_to_ifstream(filename);
        input->seekg(0, std::ifstream::end);
        bytes.resize(static_cast<std::size_t>(input->tellg()));
        input->seekg(0, std::ifstream::beg);
        input->read(&bytes[0], bytes.size());
    }
    std::string output_bytes;
    {
        const auto header = sepia::read_header(sepia::filename_to_ifstream(filename));
        std::ostringstream output;
        sepia::join_observable<sepia::type::atis>(
            sepia::filename_to_ifstream(filename),
            sepia::write_to_reference<sepia::type::atis>(output, header.width, header.height));
        output_bytes = output.str();
    }
    REQUIRE(bytes.size() == output_bytes.size());
    REQUIRE(std::strcmp(bytes.c_str(), output_bytes.c_str()) == 0);
}

TEST_CASE("write color events", "[sepia::write<sepia::type::color>]") {
    const auto filename = sepia::join({examples, "color.es"});
    std::string bytes;
    {
        auto input = sepia::filename_to_ifstream(filename);
        input->seekg(0, std::ifstream::end);
        bytes.resize(static_cast<std::size_t>(input->tellg()));
        input->seekg(0, std::ifstream::beg);
        input->read(&bytes[0], bytes.size());
    }
    std::string output_bytes;
    {
        const auto header = sepia::read_header(sepia::filename_to_ifstream(filename));
        std::ostringstream output;
        sepia::join_observable<sepia::type::color>(
            sepia::filename_to_ifstream(filename),
            sepia::write_to_reference<sepia::type::color>(output, header.width, header.height));
        output_bytes = output.str();
    }
    REQUIRE(bytes.size() == output_bytes.size());
    REQUIRE(std::strcmp(bytes.c_str(), output_bytes.c_str()) == 0);
}

TEST_CASE("parse JSON parameters", "[sepia::parameter]") {
    auto parameter = sepia::make_unique<sepia::object_parameter>(
        "key 0",
        sepia::array_parameter::make_empty(sepia::make_unique<sepia::char_parameter>(0)),
        "key 1",
        sepia::make_unique<sepia::object_parameter>(
            "subkey 0",
            sepia::make_unique<sepia::enum_parameter>("r", std::unordered_set<std::string>{"r", "g", "b"}),
            "subkey 1",
            sepia::make_unique<sepia::number_parameter>(0, 0, 1, false),
            "subkey 2",
            sepia::make_unique<sepia::number_parameter>(0, 0, 1000, true),
            "subkey 3",
            sepia::make_unique<sepia::boolean_parameter>(false)));
    parameter->parse(sepia::make_unique<std::stringstream>(R""(
        {
            "key 0": [0, 10, 20],
            "key 1": {
                "subkey 0": "g",
                "subkey 1": 5e-2,
                "subkey 2": 500,
                "subkey 3": true
            }
        }
    )""));
}
