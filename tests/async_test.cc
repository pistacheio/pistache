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

    auto p5 = doAsync(10);
    p5
        .then([](int result) { return result * 3.51; }, Async::NoExcept)
        .then([](double result) { ASSERT_EQ(result, 20 * 3.51); }, Async::NoExcept);

    auto p6 = doAsync(20);
    p6
        .then([](int result) { return doAsync(result - 5); }, Async::NoExcept)
        .then([](int result) { ASSERT_EQ(result, 70); }, Async::NoExcept);

    std::this_thread::sleep_for(std::chrono::seconds(2));
}

TEST(async_test, when_all) {
    auto p1 = Async::Promise<int>::resolved(10);
    int p2 = 123;
    auto p3 = Async::Promise<std::string>::resolved("Hello");
    auto p4 = Async::Promise<void>::resolved();

    bool resolved { false };

    Async::whenAll(p1, p2, p3).then([&](const std::tuple<int, int, std::string>& results) {
        resolved = true;
        ASSERT_EQ(std::get<0>(results), 10);
        ASSERT_EQ(std::get<1>(results), 123);
        ASSERT_EQ(std::get<2>(results), "Hello");
    }, Async::NoExcept);

    ASSERT_TRUE(resolved);

    std::vector<Async::Promise<int>> vec;
    vec.push_back(std::move(p1));
    vec.push_back(Async::Promise<int>::resolved(p2));

    resolved = false;

    Async::whenAll(std::begin(vec), std::end(vec)).then([&](const std::vector<int>& results) {
        resolved = true;
        ASSERT_EQ(results.size(), 2);
        ASSERT_EQ(results[0], 10);
        ASSERT_EQ(results[1], 123);
    },
    Async::NoExcept);

    ASSERT_TRUE(resolved);

    auto p5 = doAsync(10);
    auto p6 = p5.then([](int result) { return result * 3.1415; }, Async::NoExcept);

    resolved = false;

    Async::whenAll(p5, p6).then([&](std::tuple<int, double> results) {
        ASSERT_EQ(std::get<0>(results), 20);
        ASSERT_EQ(std::get<1>(results), 20 * 3.1415);
        resolved = true;
    }, Async::NoExcept);

    std::this_thread::sleep_for(std::chrono::seconds(3));
    ASSERT_TRUE(resolved);

    // @Todo: does not compile yet. Figure out why it does not compile with void
    // promises
#if 0
    Async::whenAll(p3, p4).then([](const std::tuple<std::string, void>& results) {
    }, Async::NoExcept);
#endif

}
