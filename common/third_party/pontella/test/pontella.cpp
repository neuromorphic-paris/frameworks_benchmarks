#define CATCH_CONFIG_MAIN
#include "../source/pontella.hpp"
#include "../third_party/Catch2/single_include/catch.hpp"

TEST_CASE("Parse a valid command line", "[parse]") {
    for (const auto& first_option_parts : std::vector<std::vector<const char*>>({
             {"--verbose=1"},
             {"-verbose=1"},
             {"--v=1"},
             {"-v=1"},
             {"--verbose", "1"},
             {"-verbose", "1"},
             {"--v", "1"},
             {"-v", "1"},
         })) {
        for (const auto& second_option : {
                 "--help",
                 "-help",
                 "--h",
                 "-h",
             }) {
            for (std::size_t permutation_index = 0; permutation_index < 6; ++permutation_index) {
                std::vector<const char*> arguments{"./program"};
                switch (permutation_index) {
                    case 0: {
                        arguments.push_back("input.log");
                        for (const auto& first_option_part : first_option_parts) {
                            arguments.push_back(first_option_part);
                        }
                        arguments.push_back(second_option);
                        break;
                    }
                    case 1: {
                        arguments.push_back("input.log");
                        arguments.push_back(second_option);
                        for (const auto& first_option_part : first_option_parts) {
                            arguments.push_back(first_option_part);
                        }
                        break;
                    }
                    case 2: {
                        for (const auto& first_option_part : first_option_parts) {
                            arguments.push_back(first_option_part);
                        }
                        arguments.push_back("input.log");
                        arguments.push_back(second_option);
                        break;
                    }
                    case 3: {
                        for (const auto& first_option_part : first_option_parts) {
                            arguments.push_back(first_option_part);
                        }
                        arguments.push_back(second_option);
                        arguments.push_back("input.log");
                        break;
                    }
                    case 4: {
                        arguments.push_back(second_option);
                        arguments.push_back("input.log");
                        for (const auto& first_option_part : first_option_parts) {
                            arguments.push_back(first_option_part);
                        }
                        break;
                    }
                    case 5: {
                        arguments.push_back(second_option);
                        for (const auto& first_option_part : first_option_parts) {
                            arguments.push_back(first_option_part);
                        }
                        arguments.push_back("input.log");
                        break;
                    }
                    default: { break; }
                }
                auto command = pontella::parse(
                    static_cast<int>(arguments.size()),
                    const_cast<char**>(arguments.data()),
                    1,
                    {
                        {"verbose", {"v"}},
                    },
                    {
                        {"help", {"h"}},
                    });
                REQUIRE(command.arguments.size() == 1);
                REQUIRE(command.arguments.front() == "input.log");
                REQUIRE(command.options.size() == 1);
                REQUIRE(command.options.find("verbose") != command.options.end());
                REQUIRE(command.options["verbose"] == "1");
                REQUIRE(command.flags.find("help") != command.flags.end());
            }
        }
    }
}

TEST_CASE("Fail on too many arguments", "[parse]") {
    std::vector<const char*> arguments{"./program", "input.log"};
    REQUIRE_THROWS(
        pontella::parse(static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), 0, {}, {}));
}

TEST_CASE("Fail on not enough arguments", "[parse]") {
    std::vector<const char*> arguments = {"./program", "input.log"};
    REQUIRE_THROWS(
        pontella::parse(static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), 2, {}, {}));
}

TEST_CASE("Fail on options with the same name", "[parse]") {
    auto arguments = std::vector<const char*>({"./program"});
    REQUIRE_THROWS(pontella::parse(
        static_cast<int>(arguments.size()),
        const_cast<char**>(arguments.data()),
        0,
        {},
        {
            {"help", {"h1"}},
            {"help", {"h2"}},
        }));
}

TEST_CASE("Fail on options with the same alias", "[parse]") {
    std::vector<const char*> arguments{"./program"};
    REQUIRE_THROWS(pontella::parse(
        static_cast<int>(arguments.size()),
        const_cast<char**>(arguments.data()),
        0,
        {
            {"hidden", {"h"}},
        },
        {
            {"help", {"h"}},
        }));
}

TEST_CASE("Fail on flag with a parameter", "[parse]") {
    for (const auto& option : {"--help=true", "-help=true", "--h=true", "-h=true"}) {
        std::vector<const char*> arguments{"./program", option};
        REQUIRE_THROWS(pontella::parse(
            static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), 0, {}, {{"help", {"h"}}}));
    }
}

TEST_CASE("Fail on option without a parameter", "[parse]") {
    for (const auto& option : {"--verbose", "-verbose", "--v", "-v"}) {
        std::vector<const char*> arguments{"./program", option};
        REQUIRE_THROWS(pontella::parse(
            static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), 0, {{"verbose", {"v"}}}, {}));
    }
}

TEST_CASE("Fail on unknown option", "[parse]") {
    for (const auto& option : {"--verbose", "-verbose", "--v", "-v"}) {
        std::vector<const char*> arguments{"./program", option};
        REQUIRE_THROWS(pontella::parse(
            static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), 0, {}, {{"help", {"h"}}}));
    }
}

TEST_CASE("Fail on unexpected characters", "[parse]") {
    for (const auto& option : {"-", "--"}) {
        std::vector<const char*> arguments{"./program", option};
        REQUIRE_THROWS(pontella::parse(
            static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), 0, {}, {{"help", {"h"}}}));
    }
}

TEST_CASE("Test a command line for a flag", "[test]") {
    for (const auto& option : {"--help", "-help", "--h", "-h"}) {
        std::vector<const char*> arguments{"./program", option};
        REQUIRE(
            pontella::test(static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), {"help", {"h"}}));
    }
    for (const auto& option : {"--help", "-help", "--h", "-h"}) {
        std::vector<const char*> arguments{"./program", option};
        REQUIRE(!pontella::test(
            static_cast<int>(arguments.size()), const_cast<char**>(arguments.data()), {"verbose", {"v"}}));
    }
}

TEST_CASE("Test the main wrapper", "[main]") {
    {
        std::vector<const char*> arguments{"./program"};
        REQUIRE(
            pontella::main(
                {},
                static_cast<int>(arguments.size()),
                const_cast<char**>(arguments.data()),
                0,
                {},
                {},
                [](pontella::command) {})
            == 0);
    }
    for (const auto& option : {"--help", "-help", "--h", "-h"}) {
        auto arguments = std::vector<const char*>({"./program", option});
        REQUIRE(
            pontella::main(
                {},
                static_cast<int>(arguments.size()),
                const_cast<char**>(arguments.data()),
                0,
                {},
                {},
                [](pontella::command) {})
            == 1);
    }
    {
        std::vector<const char*> arguments{"./program"};
        REQUIRE(
            pontella::main(
                {},
                static_cast<int>(arguments.size()),
                const_cast<char**>(arguments.data()),
                0,
                {},
                {},
                [](pontella::command) { throw std::runtime_error("This program always errors"); })
            == 1);
    }
}
