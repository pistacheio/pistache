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

    auto p4 = Async::Promise<int>::resolved(10);
    ASSERT_TRUE(p4.isFulfilled());

    auto p5 = Async::Promise<void>::resolved();
    ASSERT_TRUE(p5.isFulfilled());

    auto p6 = Async::Promise<int>::rejected(std::invalid_argument("Invalid"));
    ASSERT_TRUE(p6.isRejected());
}

TEST(async_test, void_promise) {
    Async::Promise<void> p1(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve();
    }); 

    ASSERT_TRUE(p1.isFulfilled());

    bool thenCalled { false };
    p1.then([&]() {
        thenCalled = true;
    }, Async::NoExcept);

    ASSERT_TRUE(thenCalled);

    Async::Promise<int> p2(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            ASSERT_THROW(resolve(), Async::Error);
    });

    Async::Promise<void> p3(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            ASSERT_THROW(resolve(10), Async::Error);
    });
}

TEST(async_test, chain_test) {
    Async::Promise<int> p1(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve(10);
    });

    p1
     .then([](int result) { return result * 2; }, Async::NoExcept)
     .then([](int result) { std::cout << "Result = " << result << std::endl; },
             Async::NoExcept);

    Async::Promise<int> p2(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve(10);
    });

    p2
     .then([](int result) { return result * 2.2901; }, Async::IgnoreException)
     .then([](double result) { std::cout << "Result = " << result << std::endl; },
             Async::IgnoreException);

    enum class Test { Foo, Bar };

    Async::Promise<Test> p3(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve(Test::Foo);
    });

    p3
        .then([](Test result) {
            return Async::Promise<std::string>(
                [=](Async::Resolver& resolve, Async::Rejection&) {
                    switch (result) {
                        case Test::Foo:
                            resolve(std::string("Foo"));
                            break;
                        case Test::Bar:
                            resolve(std::string("Bar"));
                    }
            }); }, Async::NoExcept)
        .then([](std::string str) {
                ASSERT_EQ(str, "Foo");
    }, Async::NoExcept);

    Async::Promise<Test> p4(
        [](Async::Resolver& resolve, Async::Rejection& reject) {
            resolve(Test::Bar);
    });

    p4
        .then(
        [](Test result) {
            return Async::Promise<std::string>(
                [=](Async::Resolver& resolve, Async::Rejection& reject) {
                    switch (result) {
                        case Test::Foo:
                            resolve(std::string("Foo"));
                            break;
                        case Test::Bar:
                            reject(std::runtime_error("Invalid"));
                    }
            }); 
        },
            Async::NoExcept)
        .then(
        [](std::string str) {
            ASSERT_TRUE(false);
        },
        [](std::exception_ptr exc) {
            ASSERT_THROW(std::rethrow_exception(exc), std::runtime_error);
        });

}

