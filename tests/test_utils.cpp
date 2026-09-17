// Unit tests for chunk utils: ThreadPool, TimestampUtils, ResponseBuilder.
#include "utils/DistanceUtils.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/ThreadPool.hpp"
#include "utils/TimestampUtils.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

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

TEST(Timestamps, ConcurrentFormatIsRaceFree)
{
    // Regression (TSan live run): getCurrentTimestamp used std::localtime
    // (shared static buffer) and raced across ThreadPool event batches.
    // Hammer it from 8 threads; under TSan any race fails loudly.
    constexpr int kThreads = 8;
    constexpr int kIters = 500;
    std::vector<std::thread> threads;
    std::atomic<int> bad{0};
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&]
            {
                for (int j = 0; j < kIters; ++j)
                {
                    std::string s = TimestampUtils::getCurrentTimestamp();
                    // "YYYY-MM-DD HH:MM:SS.mmm" — garbled static-buffer output
                    // would break length/shape; 23 chars when well-formed.
                    if (s.size() != 23 || s[4] != '-' || s[13] != ':')
                        ++bad;
                }
            });
    }
    for (auto &t : threads)
        t.join();
    EXPECT_EQ(bad.load(), 0);
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

TEST(DistanceUtils, Dist2DKnownValues)
{
    // Pin the single implementation all six former copies shared (3-4-5).
    PositionStruct a, b;
    a.positionX = 0.0f;
    a.positionY = 0.0f;
    b.positionX = 3.0f;
    b.positionY = 4.0f;
    EXPECT_FLOAT_EQ(DistanceUtils::dist2D(a, b), 5.0f);
    EXPECT_FLOAT_EQ(DistanceUtils::dist2D(b, a), 5.0f); // symmetric
    EXPECT_FLOAT_EQ(DistanceUtils::dist2D(a, a), 0.0f);
    // Z is ignored by the 2D variant (movement/aggro plane).
    b.positionZ = 100.0f;
    EXPECT_FLOAT_EQ(DistanceUtils::dist2D(a, b), 5.0f);
}

TEST(DistanceUtils, Dist3DKnownValues)
{
    // The AttackSystem variant: Z counts (kept 1-1, not "fixed" here).
    PositionStruct a, b;
    a.positionX = 1.0f;
    a.positionY = 2.0f;
    a.positionZ = 2.0f;
    b.positionX = 1.0f;
    b.positionY = 2.0f;
    b.positionZ = 6.0f;
    EXPECT_FLOAT_EQ(DistanceUtils::dist3D(a, b), 4.0f);
    EXPECT_FLOAT_EQ(DistanceUtils::dist3D(a, a), 0.0f);
}

TEST(DistanceUtils, WithinRangeMatchesSqrtCompare)
{
    // Wave 3.1: the vendor (3D) and dialogue (2D) range checks now share
    // these. Pin against naive sqrt comparison away from the ulp boundary.
    PositionStruct a, b;
    a.positionX = 0.0f;
    a.positionY = 0.0f;
    a.positionZ = 0.0f;
    b.positionX = 30.0f;
    b.positionY = 40.0f;
    b.positionZ = 0.0f;
    EXPECT_TRUE(DistanceUtils::withinRange2D(a, b, 50.5f));
    EXPECT_FALSE(DistanceUtils::withinRange2D(a, b, 49.5f));
    EXPECT_TRUE(DistanceUtils::withinRange3D(a, b, 50.5f));
    EXPECT_FALSE(DistanceUtils::withinRange3D(a, b, 49.5f));
    b.positionZ = 100.0f; // Z splits the two: 2D still in, 3D out
    EXPECT_TRUE(DistanceUtils::withinRange2D(a, b, 50.5f));
    EXPECT_FALSE(DistanceUtils::withinRange3D(a, b, 50.5f));
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
