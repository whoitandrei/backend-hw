#include "semaphore.hpp"

#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace semaphore {

Semaphore::Semaphore(int n) {
    if (n < 0) {
        throw std::invalid_argument("число разрешений не может быть отрицательным");
    }
    permits.store(static_cast<uint32_t>(n), std::memory_order_relaxed);
}

void Semaphore::Acquire() {
    while (true) {
        auto cur_state = permits.load(std::memory_order_relaxed);
        if (cur_state != 0) {
            if (permits.compare_exchange_weak(cur_state, cur_state - 1, std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
                return;
            }
            continue;
        }

        waiters.fetch_add(1);
        if (permits.load() == 0) {
            futex::Wait(&permits, 0);
        }
        waiters.fetch_sub(1);
    }
}

bool Semaphore::TryAcquire() {
    while (true) {
        auto cur_state = permits.load(std::memory_order_relaxed);
        if (cur_state == 0) return false;
        if (permits.compare_exchange_weak(cur_state, cur_state - 1, std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
            return true;
        }
    }
}

void Semaphore::Release() {
    permits.fetch_add(1);
    if (waiters.load() != 0) {
        futex::Wake(&permits);
    }
}

int Semaphore::Available() const {
    return static_cast<int>(permits.load(std::memory_order_relaxed));
}

}  // namespace semaphore
