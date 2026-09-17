// Unit tests for SpawnGeometry (A1 single source of truth).
//
// Pins the sampling contracts shared by mob spawn, respawn points and
// champion placement: bounds, disc containment, annulus hole, sector
// spread, degenerate anchors. Distribution shape (uniform/equal-area) is
// asserted statistically, never by exact values.
#include "utils/RandomUtils.hpp"
#include "utils/SpawnGeometry.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

namespace
{

float dist2(float x1, float y1, float x2, float y2)
{
    const float dx = x1 - x2;
    const float dy = y1 - y2;
    return dx * dx + dy * dy;
}

} // namespace

TEST(SpawnGeometry, RectStaysInBounds)
{
    RandomUtils::seedForTests(42u);
    for (int i = 0; i < 200; ++i)
    {
        const auto [x, y] = SpawnGeometry::sampleRect(10.0f, 20.0f, -5.0f, 5.0f);
        EXPECT_GE(x, 10.0f);
        EXPECT_LE(x, 20.0f);
        EXPECT_GE(y, -5.0f);
        EXPECT_LE(y, 5.0f);
    }
}

TEST(SpawnGeometry, RectDegenerateAxisIsFixedPoint)
{
    RandomUtils::seedForTests(42u);
    for (int i = 0; i < 50; ++i)
    {
        const auto [x, y] = SpawnGeometry::sampleRect(7.0f, 7.0f, 0.0f, 10.0f);
        EXPECT_FLOAT_EQ(x, 7.0f);
        EXPECT_GE(y, 0.0f);
        EXPECT_LE(y, 10.0f);
    }
}

TEST(SpawnGeometry, CircleStaysInDisc)
{
    RandomUtils::seedForTests(42u);
    for (int i = 0; i < 300; ++i)
    {
        const auto [x, y] = SpawnGeometry::sampleCircle(100.0f, 200.0f, 50.0f);
        EXPECT_LE(dist2(x, y, 100.0f, 200.0f), 50.0f * 50.0f);
    }
}

TEST(SpawnGeometry, CircleUniformCoversArea)
{
    // Uniform-over-disc must reach both the rim and the centre over samples
    // (a degenerate r==const bug would fail one of the two).
    RandomUtils::seedForTests(42u);
    bool nearRim = false;
    bool nearCentre = false;
    for (int i = 0; i < 500; ++i)
    {
        const auto [x, y] = SpawnGeometry::sampleCircle(0.0f, 0.0f, 100.0f);
        const float d2 = dist2(x, y, 0.0f, 0.0f);
        if (d2 > 90.0f * 90.0f)
            nearRim = true;
        if (d2 < 20.0f * 20.0f)
            nearCentre = true;
    }
    EXPECT_TRUE(nearRim);
    EXPECT_TRUE(nearCentre);
}

TEST(SpawnGeometry, CircleDegenerateReturnsCentre)
{
    const auto [x, y] = SpawnGeometry::sampleCircle(5.0f, 6.0f, 0.0f);
    EXPECT_FLOAT_EQ(x, 5.0f);
    EXPECT_FLOAT_EQ(y, 6.0f);
}

TEST(SpawnGeometry, AnnulusRespectsHole)
{
    RandomUtils::seedForTests(42u);
    for (int i = 0; i < 300; ++i)
    {
        const auto [x, y] = SpawnGeometry::sampleAnnulus(0.0f, 0.0f, 30.0f, 50.0f);
        const float d2 = dist2(x, y, 0.0f, 0.0f);
        EXPECT_GE(d2, 30.0f * 30.0f);
        EXPECT_LE(d2, 50.0f * 50.0f);
    }
}

TEST(SpawnGeometry, AnnulusDegenerateReturnsCentre)
{
    const auto [x, y] = SpawnGeometry::sampleAnnulus(5.0f, 6.0f, 40.0f, 40.0f);
    EXPECT_FLOAT_EQ(x, 5.0f);
    EXPECT_FLOAT_EQ(y, 6.0f);
}

TEST(SpawnGeometry, SectorSpreadAcrossSlots)
{
    // 4 slots must land in 4 distinct quadrants around the centre.
    RandomUtils::seedForTests(42u);
    bool quadrants[4] = {false, false, false, false};
    for (int slot = 0; slot < 4; ++slot)
    {
        const auto [x, y] = SpawnGeometry::sampleAnnulusSector(0.0f, 0.0f, 80.0f, 100.0f, slot, 4);
        const float d2 = dist2(x, y, 0.0f, 0.0f);
        EXPECT_GE(d2, 80.0f * 80.0f);
        EXPECT_LE(d2, 100.0f * 100.0f);
        const float angle = std::atan2(y, x);
        const float norm = (angle < 0.0f) ? angle + 2.0f * 3.14159265358979323846f : angle;
        const int q = static_cast<int>(norm / (2.0f * 3.14159265358979323846f / 4.0f));
        ASSERT_GE(q, 0);
        ASSERT_LT(q, 4);
        quadrants[q] = true;
    }
    for (bool seen : quadrants)
        EXPECT_TRUE(seen);
}

TEST(SpawnGeometry, SectorDegenerateSlotsFallBack)
{
    // totalSlots<1 degrades to plain annulus sampling (still in-ring).
    RandomUtils::seedForTests(42u);
    const auto [x, y] = SpawnGeometry::sampleAnnulusSector(0.0f, 0.0f, 30.0f, 50.0f, 0, 0);
    const float d2 = dist2(x, y, 0.0f, 0.0f);
    EXPECT_GE(d2, 30.0f * 30.0f);
    EXPECT_LE(d2, 50.0f * 50.0f);
}
