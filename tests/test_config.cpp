// Unit tests for layered Config (defaults < config.json < ENV) + validation.
#include "utils/Config.hpp"
#include "utils/ProtocolLimits.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {

void clearEnv() {
    unsetenv("GAME_SERVER_HOST");
    unsetenv("GAME_SERVER_PORT");
    unsetenv("GAME_SERVER_MAX_CLIENTS");
    unsetenv("SERVER_HOST");
    unsetenv("SERVER_PORT");
    unsetenv("SERVER_MAX_CLIENTS");
    unsetenv("CHUNK_PUBLIC_HOST");
}

struct EnvGuard {
    EnvGuard() { clearEnv(); }
    ~EnvGuard() { clearEnv(); }
};

std::string writeTempJson(const std::string &body) {
    char tmpl[] = "/tmp/mmo_chunk_cfg_XXXXXX";
    int fd = mkstemp(tmpl);
    EXPECT_NE(fd, -1);
    {
        std::ofstream f(tmpl);
        f << body;
    }
    close(fd);
    return std::string(tmpl);
}

} // namespace

TEST(Config, DefaultsWhenNoFileNoEnv) {
    EnvGuard g;
    Config c;
    auto [gs, cs] = c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
    EXPECT_EQ(gs.port, 27016);
    EXPECT_EQ(cs.port, 27017);
    EXPECT_EQ(cs.max_clients, 3000);
    // publicHost falls back to host when unset.
    EXPECT_FALSE(cs.publicHost.empty());
}

TEST(Config, InvalidPortThrowsWithVarName) {
    EnvGuard g;
    setenv("SERVER_PORT", "bogus", 1);
    Config c;
    try {
        c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("SERVER_PORT"), std::string::npos);
    }
}

TEST(Config, PublicHostDefaultsToHost) {
    EnvGuard g;
    setenv("SERVER_HOST", "0.0.0.0", 1);
    Config c;
    auto [gs, cs] = c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
    EXPECT_EQ(cs.publicHost, "0.0.0.0");
    setenv("CHUNK_PUBLIC_HOST", "203.0.113.7", 1);
    auto [gs2, cs2] = c.parseConfig("/tmp/mmo_cfg_definitely_missing_12345.json");
    EXPECT_EQ(cs2.publicHost, "203.0.113.7");
}

TEST(ProtocolLimits, ValuesMatchHarness) {
    EXPECT_EQ(protocol::kMaxMessageBytes, 8u * 1024u);
    EXPECT_STREQ(protocol::kErrOversize, "ERR_OVERSIZE");
    EXPECT_STREQ(protocol::kErrMalformed, "ERR_MALFORMED");
}
