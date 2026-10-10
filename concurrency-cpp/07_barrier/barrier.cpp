#include "barrier.hpp"

#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace barrier {

Barrier::Barrier(int n) {
    if (n <= 0) {
        throw std::invalid_argument("участников барьера должно быть хотя бы один");
    }
    need = static_cast<uint32_t>(n);
}

void Barrier::Wait() {
    auto cur_roung = round.load(std::memory_order_acquire);
    auto cur_arrived = arrived.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (cur_arrived == need) {
        arrived.store(0, std::memory_order_relaxed);
        round.fetch_add(1, std::memory_order_release);
        futex::WakeAll(&round);
        return;
    } else {
        while (true) {
            futex::Wait(&round, cur_roung);
            if (cur_roung != round.load(std::memory_order_acquire)) return;
        }
    }
}

}  // namespace barrier
