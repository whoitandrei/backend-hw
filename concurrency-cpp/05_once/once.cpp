#include "once.hpp"

#include <stdexcept>

#include "internal/futex/futex.hpp"

constexpr uint32_t runnable = 0;
constexpr uint32_t running = 1;
constexpr uint32_t runned = 2;

namespace once {

void Once::Do(const std::function<void()>& f) {
    auto status = state.load(std::memory_order_acquire);
    if (status == runned) return;
    if (status == runnable) {
        if (state.compare_exchange_strong(status, running, std::memory_order_acquire,
                                          std::memory_order_acquire)) {
            try {
                f();
            } catch (...) {
                state.store(runned);
                if (waiters.load() != 0) futex::WakeAll(&state);
                throw;
            }
            state.store(runned);
            if (waiters.load() != 0) futex::WakeAll(&state);
        }
    }
    while (true) {
        status = state.load(std::memory_order_acquire);
        if (status == runned) return;
        waiters.fetch_add(1);
        if (state.load() != runned) futex::Wait(&state, running);
        waiters.fetch_sub(1);
    }
}

bool Once::Done() const {
    return state.load(std::memory_order_acquire) == runned;
}

}  // namespace once
