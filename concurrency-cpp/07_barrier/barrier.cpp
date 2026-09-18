#include "barrier.hpp"

#include <stdexcept>

#include "internal/futex/futex.hpp"

namespace barrier {

Barrier::Barrier(int n) : need(n) {}

void Barrier::Wait() {
  auto cur_roung = round.load();
  auto cur_arrived = arrived.fetch_add(1) + 1;
  if (cur_arrived == need) {
    arrived.store(0);
    round.fetch_add(1);
    futex::WakeAll(&round);
    return;
  } else {
    while (true) {
      futex::Wait(&round, cur_roung);
      if (cur_roung != round.load()) return;
    }
  }
}

} // namespace barrier
