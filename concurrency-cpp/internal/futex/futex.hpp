#pragma once

#include <atomic>
#include <cstdint>

namespace futex {

void Wait(std::atomic<uint32_t>* addr, uint32_t val);
void Wake(std::atomic<uint32_t>* addr);
void WakeAll(std::atomic<uint32_t>* addr);

}  // namespace futex
