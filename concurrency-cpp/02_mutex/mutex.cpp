#include "mutex.hpp"

#include <cstdint>
#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace mutex {

void Mutex::Lock() {
    uint32_t expected = free;
    if (state.compare_exchange_strong(expected, held)) return;

    while(state.exchange(contended) != free) {
        futex::Wait(&state, contended);
    }
}

bool Mutex::TryLock() {
    uint32_t expected = free;
    return state.compare_exchange_strong(expected, held);    
}

void Mutex::Unlock() {
    auto prev_state = state.exchange(free);
    if (prev_state == free) {
        throw std::logic_error("Unlock без Lock");
    }
    if (prev_state == contended) {
        futex::Wake(&state);
    }
}

}  // namespace mutex
