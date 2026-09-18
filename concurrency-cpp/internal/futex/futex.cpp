#include "internal/futex/futex.hpp"

static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t));
static_assert(alignof(std::atomic<uint32_t>) == alignof(uint32_t));

#if defined(__linux__)

#include <climits>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace futex {

void Wait(std::atomic<uint32_t>* addr, uint32_t val) {
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
            val, nullptr, nullptr, 0);
}

void Wake(std::atomic<uint32_t>* addr) {
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
            1, nullptr, nullptr, 0);
}

void WakeAll(std::atomic<uint32_t>* addr) {
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
            INT_MAX, nullptr, nullptr, 0);
}

}  // namespace futex

#else

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace futex {
namespace {

struct waiter {
    std::condition_variable cv;
    bool signaled = false;
};

std::mutex mu;
std::map<const void*, std::vector<std::shared_ptr<waiter>>> waiters;

}  // namespace

void Wait(std::atomic<uint32_t>* addr, uint32_t val) {
    std::unique_lock<std::mutex> lock(mu);
    if (addr->load() != val) {
        return;
    }

    auto w = std::make_shared<waiter>();
    waiters[addr].push_back(w);
    w->cv.wait(lock, [&w] { return w->signaled; });
}

void Wake(std::atomic<uint32_t>* addr) {
    std::unique_lock<std::mutex> lock(mu);

    auto it = waiters.find(addr);
    if (it == waiters.end()) {
        return;
    }

    auto w = it->second.front();
    if (it->second.size() == 1) {
        waiters.erase(it);
    } else {
        it->second.erase(it->second.begin());
    }
    w->signaled = true;

    lock.unlock();
    w->cv.notify_one();
}

void WakeAll(std::atomic<uint32_t>* addr) {
    std::unique_lock<std::mutex> lock(mu);

    auto it = waiters.find(addr);
    if (it == waiters.end()) {
        return;
    }

    auto list = std::move(it->second);
    waiters.erase(it);
    for (auto& w : list) {
        w->signaled = true;
    }

    lock.unlock();
    for (auto& w : list) {
        w->cv.notify_one();
    }
}

}  // namespace futex

#endif
