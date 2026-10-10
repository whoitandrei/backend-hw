#pragma once

#include <atomic>
#include <cstdint>

namespace rwmutex {

constexpr uint32_t writer = 1u << 31;
constexpr uint32_t pending = 1u << 30;

class RWMutex {
public:
    void RLock();
    void RUnlock();
    void Lock();
    void Unlock();

private:
    std::atomic<uint32_t> state{0};
    std::atomic<uint32_t> seq{0};
    std::atomic<uint32_t> readers_waiting{0};
    std::atomic<uint32_t> writers_waiting{0};
};

}  // namespace rwmutex
