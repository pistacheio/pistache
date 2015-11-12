#include "gtest/gtest.h"
#include "async.h"
#include <thread>

Async::Promise<int> doAsync(int N)
{
    Async::Promise<int> promise(
        [=](Async::Resolver& resolve, Async::Rejection& reject) {
            std::thread thr([=]() mutable {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                resolve(N * 2);
            });

            thr.detach();
    });

    return promise;
}


TEST(async_test, basic_test) {
    Async::Promise<int> p1(
        [](Async::Resolver& resolv, Async::Rejection& reject) {
            resolv(10);
    });

    ASSERT_TRUE(p1.isFulfilled());

    int val { 0 };
    p1.then([&](int v) { val = v; }, Async::NoExcept);
    ASSERT_EQ(val, 10);

    {
        Async::Promise<int> p2 = doAsync(10);
        p2.then([](int result) { ASSERT_EQ(result, 20); },
                Async::NoExcept);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    Async::Promise<int> p3(
        [](Async::Resolver& resolv, Async::Rejection& reject) {
            reject(std::runtime_error("Because I decided"));
    });

    ASSERT_TRUE(p3.isRejected());
    p3.then([](int) { ASSERT_TRUE(false); }, [](std::exception_ptr eptr) {
        ASSERT_THROW(std::rethrow_exception(eptr), std::runtime_error);
    });

}

TEST(async_test, chain_test) {
    Async::Promise<int> p1(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve(10);
    });

    p1
     .then([](int result) { return result * 2; }, Async::NoExcept)
     ->chain()
     .then([](int result) { std::cout << "Result = " << result << std::endl; },
             Async::NoExcept);

    Async::Promise<int> p2(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve(10);
    });

#if 0
    p2
     .then([](int result) { return result * 2.0; }, Async::IgnoreException)
     ->chain()
     .then([](double result) { std::cout << "Result = " << result << std::endl; },
             Async::IgnoreException);
#endif

}

