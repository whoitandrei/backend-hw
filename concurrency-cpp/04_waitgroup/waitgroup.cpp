#include "waitgroup.hpp"

#include <cstdint>
#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace waitgroup {

void WaitGroup::Add(int delta) {
    auto cur_data = count.load();

    while (true) {
        int64_t res = static_cast<int64_t>(cur_data) + delta;
        if (res < 0) {
            throw std::logic_error("count < 0 не может быть");
        }
        if (count.compare_exchange_strong(cur_data, static_cast<uint32_t>(res))) {
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
        auto cur = count.load();
        if (cur == 0) return;
        futex::Wait(&count, cur);
    }
}

}  // namespace waitgroup
