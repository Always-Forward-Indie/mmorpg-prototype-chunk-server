// Unit tests for OngoingActionStore (A4 extract).
//
// Pins the registry semantics 1-1 with the inline map+mutex this replaced:
// casting-slug guard read, put/erase, due-sweep (due CASTING collected and
// flipped, stale EXECUTING reaped, future CASTING kept), plus a threaded
// put/erase smoke for the TSan suite.
#include "services/OngoingActionStore.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

std::shared_ptr<CombatActionStruct> makeAction(
    int casterId, CombatActionState state, std::chrono::steady_clock::time_point end)
{
    auto a = std::make_shared<CombatActionStruct>();
    a->casterId = casterId;
    a->skillSlug = "strike";
    a->actionName = "Strike";
    a->targetId = 9001;
    a->targetType = CombatTargetType::MOB;
    a->state = state;
    a->endTime = end;
    a->cooldownPreset = true;
    return a;
}

} // namespace

TEST(OngoingActionStore, CastingSlugGuard)
{
    OngoingActionStore store;
    EXPECT_FALSE(store.castingSlug(1).has_value());
    store.put(1, makeAction(1, CombatActionState::CASTING, std::chrono::steady_clock::now()));
    auto slug = store.castingSlug(1);
    ASSERT_TRUE(slug.has_value());
    EXPECT_EQ(*slug, "strike");
    // Non-casting entries do not arm the guard.
    store.put(2, makeAction(2, CombatActionState::EXECUTING, std::chrono::steady_clock::now()));
    EXPECT_FALSE(store.castingSlug(2).has_value());
}

TEST(OngoingActionStore, PutEraseSize)
{
    OngoingActionStore store;
    EXPECT_EQ(store.size(), 0u);
    store.put(1, makeAction(1, CombatActionState::CASTING, std::chrono::steady_clock::now()));
    store.put(2, makeAction(2, CombatActionState::CASTING, std::chrono::steady_clock::now()));
    EXPECT_EQ(store.size(), 2u);
    store.erase(1);
    EXPECT_EQ(store.size(), 1u);
    store.erase(424242); // missing: safe no-op
    EXPECT_EQ(store.size(), 1u);
}

TEST(OngoingActionStore, TakeDueCollectsAndSweeps)
{
    OngoingActionStore store;
    const auto past = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    const auto future = std::chrono::steady_clock::now() + std::chrono::hours(1);
    store.put(1, makeAction(1, CombatActionState::CASTING, past));   // due
    store.put(2, makeAction(2, CombatActionState::CASTING, future)); // not yet
    store.put(3, makeAction(3, CombatActionState::EXECUTING, past)); // stale

    auto due = store.takeDueAndSweep();
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0].casterId, 1);
    EXPECT_EQ(due[0].skillSlug, "strike");
    EXPECT_EQ(due[0].targetId, 9001);
    EXPECT_TRUE(due[0].cooldownPreset);
    // Due item erased, stale EXECUTING reaped, future CASTING kept.
    EXPECT_EQ(store.size(), 1u);
    EXPECT_TRUE(store.castingSlug(2).has_value());
}

TEST(OngoingActionStore, ConcurrentPutErase)
{
    OngoingActionStore store;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&store, t]
            {
                for (int i = 0; i < 250; ++i)
                {
                    const int id = t * 1000 + (i % 50);
                    store.put(id, makeAction(id, CombatActionState::CASTING, std::chrono::steady_clock::now()));
                    (void)store.castingSlug(id);
                    if (i % 2 == 0)
                        store.erase(id);
                }
            });
    }
    for (auto &th : threads)
        th.join();
    // No crash / no TSan report is the assertion; size is just sanity.
    EXPECT_LE(store.size(), 200u);
}
