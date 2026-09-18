#pragma once

#include <atomic>
#include <cstdint>

namespace waitgroup {

class WaitGroup {
public:
    void Add(int delta);
    void Done();
    void Wait();

private:
    std::atomic<uint32_t> count{0};
};

}  // namespace waitgroup
