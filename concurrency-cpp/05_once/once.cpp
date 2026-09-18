#include "once.hpp"

#include <stdexcept>

#include "internal/futex/futex.hpp"

constexpr uint32_t runnable = 0;
constexpr uint32_t running = 1;
constexpr uint32_t runned = 2;

namespace once {

void Once::Do(const std::function<void()> &f) {
  auto status = state.load();
  if (status == runned)
    return;
  if (status == runnable) {
    if (state.compare_exchange_strong(status, running)) {
      try {
        f();
      } catch (...) {
        state.store(runned);
        futex::WakeAll(&state);
        throw;
      }
      state.store(runned);
      futex::WakeAll(&state);
    }
  }
  while (true) {
    status = state.load();
    if (status == runned)
      return;
    futex::Wait(&state, running);
  }
}

bool Once::Done() const { return state.load() == runned; }

} // namespace once
