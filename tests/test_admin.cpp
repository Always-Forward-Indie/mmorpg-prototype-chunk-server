// Unit pins for the admin-RPC gate (AdminGate.hpp, pure logic — no servers).
// The compiled handler adds: #ifdef ADMIN_RPC, game_config knob, audit logs.
#include "services/AdminGate.hpp"

#include <gtest/gtest.h>

TEST(AdminGate, DisabledNeverServes)
{
    EXPECT_FALSE(admin_gate::allows(false, 7, "7"));
    EXPECT_FALSE(admin_gate::allows(false, 7, "1,7,9"));
}

TEST(AdminGate, EmptyAllowlistMatchesNobody)
{
    EXPECT_FALSE(admin_gate::allows(true, 7, ""));
    EXPECT_FALSE(admin_gate::allows(true, 0, "7"));
    EXPECT_FALSE(admin_gate::allows(true, -1, "7"));
}

TEST(AdminGate, ExactMatchOnly)
{
    EXPECT_TRUE(admin_gate::allows(true, 7, "7"));
    EXPECT_TRUE(admin_gate::allows(true, 7, "1,7,9"));
    EXPECT_TRUE(admin_gate::allows(true, 7, " 1 , 7 , 9 "));
    EXPECT_FALSE(admin_gate::allows(true, 77, "7"));
    EXPECT_FALSE(admin_gate::allows(true, 7, "77"));
    EXPECT_FALSE(admin_gate::allows(true, 8, "1,7,9"));
}

TEST(AdminGate, NonNumericTokensNeverMatchRestStillWorks)
{
    EXPECT_TRUE(admin_gate::allows(true, 7, "oops,7"));
    EXPECT_FALSE(admin_gate::allows(true, 8, "oops,7"));
}

TEST(AdminGate, KnownOps)
{
    EXPECT_TRUE(admin_gate::isKnownOp("teleport"));
    EXPECT_TRUE(admin_gate::isKnownOp("getState"));
    EXPECT_TRUE(admin_gate::isKnownOp("grantXP"));
    EXPECT_TRUE(admin_gate::isKnownOp("grantLevel"));
    EXPECT_TRUE(admin_gate::isKnownOp("grantItem"));
    EXPECT_TRUE(admin_gate::isKnownOp("setHP"));
    EXPECT_TRUE(admin_gate::isKnownOp("skipTime"));
    EXPECT_TRUE(admin_gate::isKnownOp("spawnMob"));
    EXPECT_TRUE(admin_gate::isKnownOp("killMob"));
    EXPECT_TRUE(admin_gate::isKnownOp("resetWorld"));
    EXPECT_FALSE(admin_gate::isKnownOp(""));
    EXPECT_FALSE(admin_gate::isKnownOp("dropDatabase"));
    EXPECT_FALSE(admin_gate::isKnownOp("TELEPORT"));
}
