#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace once {

class Once {
public:
    void Do(const std::function<void()>& f);
    bool Done() const;

private:
    std::atomic<uint32_t> state{0};
    std::atomic<uint32_t> waiters{0};
};

}  // namespace once
