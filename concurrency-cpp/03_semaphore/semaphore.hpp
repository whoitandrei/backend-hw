#pragma once

#include <atomic>
#include <cstdint>

namespace semaphore {

class Semaphore {
public:
    explicit Semaphore(int n);

    void Acquire();
    bool TryAcquire();
    void Release();
    int Available() const;

private:
    std::atomic<uint32_t> permits{0};
    std::atomic<uint32_t> waiters{0};
};

}  // namespace semaphore
