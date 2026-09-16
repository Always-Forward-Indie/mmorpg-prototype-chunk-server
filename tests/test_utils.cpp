// Unit tests for chunk utils: ThreadPool, TimestampUtils, ResponseBuilder.
#include "utils/ResponseBuilder.hpp"
#include "utils/ThreadPool.hpp"
#include "utils/TimestampUtils.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>

TEST(ThreadPool, RunsVoidTasks)
{
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i)
        pool.enqueueTask([&] { ++counter; });
    // Destructor joins: poll until done with a bound.
    for (int i = 0; i < 200 && counter.load() < 20; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(counter.load(), 20);
}

TEST(ThreadPool, FutureTasksReturnValues)
{
    ThreadPool pool(2);
    auto f1 = pool.enqueueTask([] { return 40 + 2; });
    auto f2 = pool.enqueueTask([](int x) { return x * 2; }, 21);
    EXPECT_EQ(f1.get(), 42);
    EXPECT_EQ(f2.get(), 42);
}

TEST(Timestamps, MonotonicMsAndEcho)
{
    long long a = TimestampUtils::getCurrentTimestampMs();
    EXPECT_GT(a, 0);
    TimestampStruct ts = TimestampUtils::createReceiveTimestamp(123, "req-1");
    EXPECT_EQ(ts.clientSendMsEcho, 123);
    EXPECT_EQ(ts.requestId, "req-1");
    EXPECT_GE(ts.serverRecvMs, a);
    TimestampUtils::setServerSendTimestamp(ts);
    EXPECT_GE(ts.serverSendMs, ts.serverRecvMs);
}

TEST(Timestamps, ExtractAndRoundtrip)
{
    nlohmann::json req;
    req["header"]["clientSendMs"] = 777;
    req["header"]["requestId"] = "sync_9";
    EXPECT_EQ(TimestampUtils::extractClientTimestamp(req), 777);
    EXPECT_EQ(TimestampUtils::extractRequestId(req), "sync_9");
    EXPECT_EQ(TimestampUtils::extractClientTimestamp(nlohmann::json::object()), 0);
    EXPECT_EQ(TimestampUtils::extractRequestId(nlohmann::json::object()), "");

    TimestampStruct ts = TimestampUtils::parseTimestampsFromRequest(req);
    EXPECT_EQ(ts.clientSendMsEcho, 777);
    EXPECT_EQ(ts.requestId, "sync_9");

    nlohmann::json resp = nlohmann::json::object();
    TimestampUtils::addTimestampsToHeader(resp, ts);
    EXPECT_EQ(resp["header"]["clientSendMsEcho"], 777);
    EXPECT_EQ(resp["header"]["requestIdEcho"], "sync_9");
    EXPECT_GE(resp["header"]["serverRecvMs"], 0);
}

TEST(ResponseBuilder, HeaderBodyAndTimestamps)
{
    ResponseBuilder b;
    b.setHeader("eventType", "pong").setBody("v", 1);
    nlohmann::json out = b.build();
    EXPECT_EQ(out["header"]["eventType"], "pong");
    EXPECT_EQ(out["body"]["v"], 1);
    EXPECT_FALSE(out["header"].contains("serverRecvMs")); // no timestamps set

    TimestampStruct ts;
    ts.serverRecvMs = 100;
    ts.serverSendMs = 120;
    ts.clientSendMsEcho = 90;
    ts.requestId = "sync_1";
    ResponseBuilder b2;
    b2.setHeader("eventType", "pong").setTimestamps(ts);
    nlohmann::json out2 = b2.build();
    EXPECT_EQ(out2["header"]["serverRecvMs"], 100);
    EXPECT_EQ(out2["header"]["serverSendMs"], 120);
    EXPECT_EQ(out2["header"]["clientSendMsEcho"], 90);
    EXPECT_EQ(out2["header"]["requestIdEcho"], "sync_1");
}
