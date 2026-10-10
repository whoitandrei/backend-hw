#include "rwmutex.hpp"

#include <cstdint>
#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace rwmutex {

void RWMutex::RLock() {
    while (true) {
        auto cur = state.load(std::memory_order_relaxed);

        if ((cur & (writer | pending)) == 0) {
            if (state.compare_exchange_weak(cur, cur + 1, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return;
            }
            continue;
        }

        readers_waiting.fetch_add(1);
        if (state.load() == cur) {
            futex::Wait(&state, cur);
        }
        readers_waiting.fetch_sub(1);
    }
}

void RWMutex::RUnlock() {
    while (true) {
        auto cur = state.load(std::memory_order_relaxed);

        if ((cur & ~(writer | pending)) == 0) {
            throw std::logic_error("читателей 0 да этого не может быть лол чел бож");
        }

        if (state.compare_exchange_weak(cur, cur - 1, std::memory_order_release,
                                        std::memory_order_relaxed)) {
            if (((cur - 1) & ~pending) == 0) {
                seq.fetch_add(1);
                if (writers_waiting.load() != 0) {
                    futex::WakeAll(&seq);
                }
            }
            return;
        }
    }
}

void RWMutex::Lock() {
    while (true) {
        auto cur = state.load(std::memory_order_relaxed);

        if ((cur & ~pending) == 0) {
            if (state.compare_exchange_weak(cur, writer, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return;
            }
            continue;
        }

        if ((cur & (writer | pending)) == 0) {
            if (!state.compare_exchange_weak(cur, cur | pending, std::memory_order_relaxed,
                                             std::memory_order_relaxed)) {
                continue;
            }
        }

        writers_waiting.fetch_add(1);
        auto ticket = seq.load();
        if ((state.load() & ~pending) != 0) {
            futex::Wait(&seq, ticket);
        }
        writers_waiting.fetch_sub(1);
    }
}

void RWMutex::Unlock() {
    uint32_t expected = writer;
    if (!state.compare_exchange_strong(expected, 0)) {
        throw std::logic_error(
            "брат писателей нет ты че освобождаешь блокировку какой ты чорт. MACAN TOP!");
    }

    if (readers_waiting.load() != 0) {
        futex::WakeAll(&state);
    }

    seq.fetch_add(1);
    if (writers_waiting.load() != 0) {
        futex::WakeAll(&seq);
    }
}

}  // namespace rwmutex
