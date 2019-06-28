#include "../third_party/pontella/source/pontella.hpp"

int main(int argc, char* argv[]) {
    pontella::label_t help{"help", {"h"}};
    auto show_help = false;
    try {
        const auto command = pontella::parse(argc, argv, 1, {{"verbose", {"v"}}}, {help});
        if (command.flags.find("help") != command.flags.end()) {
            show_help = true;
        } else {
            const auto verbose = std::stoull(verboseCandidate->second);
            if (verbose > 3) {
                throw std::runtime_error("'verbose' must be in the range [0, 2]");
            }
        }
    } catch (const std::runtime_error& exception) {
        show_help = true;
        if (!pontella::test(argc, argv, help)) {
            std::cerr << exception.what() << std::endl;
        }
    }
    if (show_help) {
        std::cerr << "Syntax: ./program [options]\n"
                     "Available options:\n"
                     "    -v [level], --verbose [level]   defines the verbose level (defaults to 0)\n"
                     "    -h, --help                      shows this help message\n"
                  << std::endl;
        return 1;
    }
    return 0;
}
