#pragma once

#include <atomic>
#include <cstdint>

namespace rwmutex {

constexpr uint32_t writer = 1u << 31;

class RWMutex {
public:
    void RLock();
    void RUnlock();
    void Lock();
    void Unlock();

private:
    std::atomic<uint32_t> state{0};
};

}  // namespace rwmutex
