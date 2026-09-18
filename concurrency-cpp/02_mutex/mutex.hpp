#pragma once

#include <atomic>
#include <cstdint>

namespace mutex {

constexpr uint32_t free = 0;
constexpr uint32_t held = 1;
constexpr uint32_t contended = 2;

class Mutex {
public:
    void Lock();
    bool TryLock();
    void Unlock();

private:
    std::atomic<uint32_t> state{free};
};

}  // namespace mutex
