#include "core/engine.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    uint32_t seed = 42;
    if (argc >= 2) seed = (uint32_t)std::atol(argv[1]);

    Engine engine;
    if (!engine.init(seed)) {
        fprintf(stderr, "Engine init failed\n");
        return 1;
    }
    engine.run();
    return 0;
}
