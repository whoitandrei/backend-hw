#include "waitgroup.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace waitgroup {

void WaitGroup::Add(int delta) {
    auto cur_data = count.load(std::memory_order_relaxed);

    while (true) {
        int64_t res = static_cast<int64_t>(cur_data) + delta;
        if (res < 0) {
            throw std::logic_error("count < 0 не может быть");
        }
        if (res > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::logic_error("count переполнился");
        }
        if (count.compare_exchange_weak(cur_data, static_cast<uint32_t>(res),
                                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
            if (res == 0) futex::WakeAll(&count);
            return;
        }
    }
}

void WaitGroup::Done() {
    this->Add(-1);
}

void WaitGroup::Wait() {
    while (true) {
        auto cur = count.load(std::memory_order_acquire);
        if (cur == 0) return;
        futex::Wait(&count, cur);
    }
}

}  // namespace waitgroup
