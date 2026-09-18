#pragma once

#include <atomic>

namespace spinlock {

class Spinlock {
public:
    void Lock();
    bool TryLock();
    void Unlock();

private:
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
};

class TTAS {
public:
    void Lock();
    bool TryLock();
    void Unlock();

private:
    std::atomic<bool> locked{false};
};

}  // namespace spinlock
