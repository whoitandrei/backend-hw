#include "semaphore.hpp"

#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace semaphore {

Semaphore::Semaphore(int n) {
    permits.store(n);
}

void Semaphore::Acquire() {
    while (true) {
        auto cur_state = permits.load();
        if (cur_state == 0) futex::Wait(&permits, 0);
        else if (permits.compare_exchange_strong(cur_state, cur_state - 1)) return;
    }
}

bool Semaphore::TryAcquire() {
    while (true) {
        auto cur_state = permits.load();
        if (cur_state == 0) return false;
        if (permits.compare_exchange_strong(cur_state, cur_state - 1)) return true;
    } 
}

void Semaphore::Release() {
    permits.fetch_add(1);
    futex::Wake(&permits);
}

int Semaphore::Available() const {
    return static_cast<int>(permits.load());
}

}  // namespace semaphore
