// Мини-аналог пакета testing из Go: регистрация тестов и бенчмарков,
// подтесты, сообщения об ошибках, флаги -run, -bench, -count, -v, -timeout.
//
// Тест объявляется макросом TEST(Имя) и получает ссылку t, бенчмарк —
// макросом BENCHMARK(Имя) и получает ссылку b.
#pragma once

#include <atomic>
#include <functional>
#include <string>

#if defined(__GNUC__)
#define TESTING_PRINTF_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TESTING_PRINTF_FORMAT(fmt, args)
#endif

namespace testing {

// Бросается из Fatal/Fatalf и ловится исполнителем теста.
struct FatalError {
    std::string message;
};

class T {
public:
    explicit T(std::string name, T* parent = nullptr)
        : name_(std::move(name)), parent_(parent) {}

    T(const T&) = delete;
    T& operator=(const T&) = delete;

    void Helper() {}

    [[noreturn]] void Fatal(const std::string& msg);
    [[noreturn]] void Fatalf(const char* format, ...) TESTING_PRINTF_FORMAT(2, 3);

    void Error(const std::string& msg);
    void Errorf(const char* format, ...) TESTING_PRINTF_FORMAT(2, 3);

    void Run(const std::string& name, const std::function<void(T&)>& f);

    bool Failed() const { return failed_.load(); }
    const std::string& Name() const { return name_; }

private:
    void mark(const std::string& msg);

    std::string name_;
    T* parent_ = nullptr;
    std::atomic<bool> failed_{false};
};

class PB {
public:
    explicit PB(long n) : remaining_(n) {}

    bool Next() {
        if (remaining_ <= 0) {
            return false;
        }
        --remaining_;
        return true;
    }

private:
    long remaining_;
};

class B {
public:
    explicit B(long n) : n_(n) {}

    long N() const { return n_; }

    void RunParallel(const std::function<void(PB&)>& body);

private:
    long n_;
};

using TestFunc = void (*)(T&);
using BenchFunc = void (*)(B&);

void registerTest(const char* name, TestFunc fn);
void registerBenchmark(const char* name, BenchFunc fn);

struct testRegistrar {
    testRegistrar(const char* name, TestFunc fn) { registerTest(name, fn); }
};

struct benchRegistrar {
    benchRegistrar(const char* name, BenchFunc fn) { registerBenchmark(name, fn); }
};

}  // namespace testing

#define TEST(name)                                                        \
    static void name(::testing::T& t);                                    \
    static const ::testing::testRegistrar name##_registered(#name, name); \
    static void name(::testing::T& t)

#define BENCHMARK(name)                                                     \
    static void name(::testing::B& b);                                      \
    static const ::testing::benchRegistrar name##_registered(#name, name);  \
    static void name(::testing::B& b)
