#pragma once

#include <atomic>
#include <cstdint>

namespace barrier {

class Barrier {
public:
    explicit Barrier(int n);

    void Wait();

private:
    uint32_t need = 0;
    std::atomic<uint32_t> arrived{0};
    std::atomic<uint32_t> round{0};
};

}  // namespace barrier
