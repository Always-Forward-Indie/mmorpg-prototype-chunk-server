// Unit tests for chunk ChunkManager (static registry, Logger only).
#include "services/ChunkManager.hpp"

#include <gtest/gtest.h>

namespace
{

ChunkInfoStruct makeChunk(int id)
{
    ChunkInfoStruct c;
    c.id = id;
    c.ip = "127.0.0.1";
    c.port = 27017 + id;
    return c;
}

} // namespace

TEST(ChunkRegistry, LoadAndLookup)
{
    Logger logger{"test"};
    ChunkManager mgr(logger);
    EXPECT_TRUE(mgr.getChunks().empty());
    mgr.loadChunkInfo(makeChunk(1));
    mgr.loadListOfAllChunks({makeChunk(2), makeChunk(3)});
    EXPECT_EQ(mgr.getChunks().size(), 3u);
    EXPECT_EQ(mgr.getChunksAsVector().size(), 3u);
    EXPECT_EQ(mgr.getChunkById(2).port, 27019);
    EXPECT_EQ(mgr.getChunkById(424242).id, 0); // miss -> empty struct
    EXPECT_EQ(mgr.getChunkByIP(3).port, 27020);
    EXPECT_EQ(mgr.getChunkByIP(424242).id, 0);
}

TEST(ChunkRegistry, ReloadOverwrites)
{
    Logger logger{"test"};
    ChunkManager mgr(logger);
    mgr.loadChunkInfo(makeChunk(1));
    ChunkInfoStruct upd = makeChunk(1);
    upd.port = 28000;
    mgr.loadChunkInfo(upd);
    EXPECT_EQ(mgr.getChunkById(1).port, 28000);
    EXPECT_EQ(mgr.getChunks().size(), 1u);
}
