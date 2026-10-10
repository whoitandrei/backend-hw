#include "mutex.hpp"

#include <cstdint>
#include <stdexcept>

#include "internal/cpu/pause.hpp"
#include "internal/futex/futex.hpp"

namespace mutex {

constexpr int spins = 40;

void Mutex::Lock() {
    uint32_t expected = free;
    if (state.compare_exchange_strong(expected, held, std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        return;
    }

    for (int i = 0; i < spins; i++) {
        if (state.load(std::memory_order_relaxed) == free) {
            expected = free;
            if (state.compare_exchange_weak(expected, held, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return;
            }
        }
        cpu::Pause();
    }

    while (state.exchange(contended, std::memory_order_acquire) != free) {
        futex::Wait(&state, contended);
    }
}

bool Mutex::TryLock() {
    uint32_t expected = free;
    return state.compare_exchange_strong(expected, held, std::memory_order_acquire,
                                         std::memory_order_relaxed);
}

void Mutex::Unlock() {
    auto prev_state = state.exchange(free, std::memory_order_release);
    if (prev_state == free) {
        throw std::logic_error("Unlock без Lock");
    }
    if (prev_state == contended) {
        futex::Wake(&state);
    }
}

}  // namespace mutex
