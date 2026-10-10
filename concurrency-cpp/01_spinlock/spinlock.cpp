#include "spinlock.hpp"

#include <stdexcept>

#include "internal/cpu/pause.hpp"

namespace spinlock {

void Spinlock::Lock() {
    while (locked.test_and_set(std::memory_order_acquire)) {
        cpu::Pause();
    }
}

bool Spinlock::TryLock() {
    return !locked.test_and_set(std::memory_order_acquire);
}

void Spinlock::Unlock() {
    if (!locked.test(std::memory_order_relaxed)) {
        throw std::logic_error("Unlock без Lock");
    }
    locked.clear(std::memory_order_release);
}

void TTAS::Lock() {
    while (true) {
        bool expected = false;

        if (locked.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
            return;
        }

        while (locked.load(std::memory_order_relaxed)) {
            cpu::Pause();
        }
    }
}

bool TTAS::TryLock() {
    bool expected = false;
    return locked.compare_exchange_strong(expected, true, std::memory_order_acquire,
                                          std::memory_order_relaxed);
}

void TTAS::Unlock() {
    if (!locked.exchange(false, std::memory_order_release)) {
        throw std::logic_error("Unlock без Lock");
    }
}

}  // namespace spinlock
