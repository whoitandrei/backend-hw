#include "spinlock.hpp"

#include <stdexcept>

namespace spinlock {

void Spinlock::Lock() {
    while (locked.test_and_set()) {}
}

bool Spinlock::TryLock() { 
    return !locked.test_and_set();
}

void Spinlock::Unlock() { 
    if (!locked.test()) {
        throw std::logic_error("Unlock без Lock");
    }
    locked.clear();
}

void TTAS::Lock() {
    while (true) {
        bool expected = false;

        if (locked.compare_exchange_weak(expected, true)) return;

        while (locked.load(std::memory_order_relaxed)) {}
    }
}

bool TTAS::TryLock() {
    bool expected = false;
    return locked.compare_exchange_weak(expected, true);
}

void TTAS::Unlock() { 
    if (!locked.exchange(false)) {
        throw std::logic_error("Unlock без Lock");
    }
}

}  // namespace spinlock
