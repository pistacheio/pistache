/*
 * SPDX-FileCopyrightText: 2021 bbarbisch
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pistache/endpoint.h>
#include <pistache/router.h>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace Pistache;

TEST(endpoint_initialization_test, initialize_options_before_handler)
{
    Rest::Router router;
    auto handler = router.handler();
    Address addr(Ipv4::any(), Port(0));
    Http::Endpoint endpoint(addr);

    size_t maxReqSize  = 123;
    size_t maxRespSize = 456;

    auto opts = Http::Endpoint::options();
    opts.threads(2);
    opts.maxRequestSize(maxReqSize);
    opts.maxResponseSize(maxRespSize);

    endpoint.init(opts);
    endpoint.setHandler(handler);

    ASSERT_EQ(handler->getMaxRequestSize(), maxReqSize);
    ASSERT_EQ(handler->getMaxResponseSize(), maxRespSize);
}

TEST(endpoint_initialization_test, initialize_handler_before_options)
{
    Rest::Router router;
    auto handler = router.handler();
    Address addr(Ipv4::any(), Port(0));
    Http::Endpoint endpoint(addr);

    size_t maxReqSize  = 123;
    size_t maxRespSize = 456;

    auto opts = Http::Endpoint::options();
    opts.threads(2);
    opts.maxRequestSize(maxReqSize);
    opts.maxResponseSize(maxRespSize);

    endpoint.setHandler(handler);
    endpoint.init(opts);

    ASSERT_EQ(handler->getMaxRequestSize(), maxReqSize);
    ASSERT_EQ(handler->getMaxResponseSize(), maxRespSize);
}

// setHandler() used to dereference the incoming handler unconditionally
// (handler_->setMaxRequestSize(...)), so passing a null shared_ptr crashed
// immediately instead of failing predictably.
TEST(endpoint_initialization_test, set_handler_rejects_null_handler)
{
    Address addr(Ipv4::any(), Port(0));
    Http::Endpoint endpoint(addr);

    ASSERT_THROW(endpoint.setHandler(nullptr), std::runtime_error);
}

// Best-effort concurrency stress test: init() writes options_, while
// setHandler() reads options_ (to propagate maxRequestSize_/
// maxResponseSize_ onto the handler) and writes handler_; both are now
// guarded by the same stateMutex_. This doesn't deterministically prove the
// guard works (that would need something like TSan, which this sandboxed
// environment can't run), but it does exercise concurrent init()/
// setHandler() calls under real contention instead of only ever calling
// them sequentially, as the two tests above do.
TEST(endpoint_initialization_test, concurrent_init_and_set_handler_does_not_crash)
{
    Address addr(Ipv4::any(), Port(0));
    Http::Endpoint endpoint(addr);

    Rest::Router router;
    auto handler = router.handler();

    constexpr int Iterations = 500;

    std::thread initThread([&endpoint] {
        for (int i = 0; i < Iterations; ++i)
        {
            auto opts = Http::Endpoint::options();
            opts.maxRequestSize(static_cast<size_t>(100 + i));
            opts.maxResponseSize(static_cast<size_t>(200 + i));
            endpoint.init(opts);
        }
    });

    std::thread setHandlerThread([&endpoint, &handler] {
        for (int i = 0; i < Iterations; ++i)
            endpoint.setHandler(handler);
    });

    initThread.join();
    setHandlerThread.join();

    SUCCEED();
}
