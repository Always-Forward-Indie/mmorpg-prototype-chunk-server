#pragma once
#include "data/DataStructs.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <shared_mutex>
#include <map>

class ChunkManager
{
public:
    ChunkManager(Logger& logger);
    void loadChunkInfo(ChunkInfoStruct chunkInfo);
    void loadListOfAllChunks(std::vector<ChunkInfoStruct> selectChunks);

    std::map<int, ChunkInfoStruct> getChunks() const;
    std::vector<ChunkInfoStruct> getChunksAsVector() const;
    ChunkInfoStruct getChunkById(int chunkId) const;
    ChunkInfoStruct getChunkByIP(int chunkId) const;

private:
    Logger& logger_;

    // Store the chunks in memory as map with chunkId as key
    std::map<int, ChunkInfoStruct> chunks_;

    // Mutex for the chunks map
    mutable std::shared_mutex mutex_;
};
