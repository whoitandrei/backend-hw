#include "barrier.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace barrier {
namespace {

using namespace std::chrono_literals;

std::future<void> finished(std::function<void()> f) {
    return std::async(std::launch::async, std::move(f));
}

bool ready(std::future<void>& f, std::chrono::nanoseconds d) {
    return f.wait_for(d) == std::future_status::ready;
}

void waitAll(std::vector<std::future<void>>& fs) {
    for (auto& f : fs) {
        f.get();
    }
}

}  // namespace

TEST(TestNobodyPassesEarly) {
    Barrier b(3);
    std::atomic<int> passed{0};

    std::vector<std::future<void>> early;
    for (int i = 0; i < 2; i++) {
        early.push_back(finished([&] {
            b.Wait();
            passed.fetch_add(1);
        }));
    }

    std::this_thread::sleep_for(100ms);
    if (const int got = passed.load(); got != 0) {
        t.Fatalf("%d участников прошли барьер до прихода всех", got);
    }

    auto last = finished([&] {
        b.Wait();
        passed.fetch_add(1);
    });

    if (!ready(last, 2s)) {
        t.Fatal("последний участник не прошёл барьер");
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (passed.load() != 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    if (const int got = passed.load(); got != 3) {
        t.Fatalf("барьер прошли %d из 3", got);
    }

    waitAll(early);
}

TEST(TestSingleParticipant) {
    Barrier b(1);

    auto waiting = finished([&b] { b.Wait(); });
    if (!ready(waiting, 1s)) {
        t.Fatal("барьер на одного не должен блокировать");
    }
}

TEST(TestReusableAcrossRounds) {
    const int parties = 6, rounds = 50;
    Barrier b(parties);

    std::atomic<int> round{0};

    std::vector<std::future<void>> workers;
    for (int i = 0; i < parties; i++) {
        workers.push_back(finished([&] {
            for (int r = 0; r < rounds; r++) {
                b.Wait();
                round.fetch_add(1);
            }
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 30s)) {
        t.Fatal("барьер завис между раундами");
    }

    if (const int want = parties * rounds; round.load() != want) {
        t.Fatalf("проходов %d, ожидалось %d", round.load(), want);
    }
}

TEST(TestRoundsDoNotOverlap) {
    const int parties = 4, rounds = 100;
    Barrier b(parties);

    std::atomic<int> inRound{0};
    std::atomic<int> bad{0};

    std::vector<std::future<void>> workers;
    for (int i = 0; i < parties; i++) {
        workers.push_back(finished([&] {
            for (int r = 0; r < rounds; r++) {
                b.Wait();

                if (inRound.fetch_add(1) + 1 > parties) {
                    bad.store(1);
                }
                std::this_thread::sleep_for(1us);
                inRound.fetch_sub(1);
            }
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 30s)) {
        t.Fatal("тест завис");
    }

    if (bad.load() != 0) {
        t.Fatal("участники следующего раунда обогнали предыдущий");
    }
}

}  // namespace barrier
