#include "rwmutex.hpp"

#include <cstdint>
#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace rwmutex {

void RWMutex::RLock() {
    while (true) {
        auto cur = state.load();
        bool is_writer = cur & writer;

        if (is_writer) {
            futex::Wait(&state, cur);
        } else {
            if (state.compare_exchange_strong(cur, cur + 1)) return;
        }
    }
}

void RWMutex::RUnlock() {
    while (true) {
        auto cur = state.load();
        uint32_t readers = cur & ~writer;

        if (readers <= 0) throw std::logic_error("читателей 0 да этого не может быть лол чел бож");

        if (state.compare_exchange_strong(cur, cur - 1)) {
            if (cur - 1 == 0) futex::WakeAll(&state);
            return;
        }
    }
}

void RWMutex::Lock() {
    while (true) {
        auto cur = state.load();
        if (cur != 0) futex::Wait(&state, cur);
        else if (state.compare_exchange_strong(cur, writer)) return;
    }
}

void RWMutex::Unlock() {
    auto cur = state.load();
    if (cur != writer) throw std::logic_error("брат писателей нет ты че освобождаешь блокировку какой ты чорт. MACAN TOP!");
    else {
        state.compare_exchange_strong(cur, 0);
        futex::WakeAll(&state);
    }
}

}  // namespace rwmutex
