#include "services/ChunkManager.hpp"

ChunkManager::ChunkManager(Logger& logger) : logger_(logger) { }

void ChunkManager::loadChunkInfo(ChunkInfoStruct chunkInfo)
{
    std::unique_lock lock(mutex_);
    chunks_[chunkInfo.id] = chunkInfo;
}

void ChunkManager::loadListOfAllChunks(std::vector<ChunkInfoStruct> selectChunks)
{
    for (auto& chunk : selectChunks) {
        loadChunkInfo(chunk);
    }
}

std::map<int, ChunkInfoStruct> ChunkManager::getChunks() const
{
    std::shared_lock lock(mutex_);
    return chunks_;
}

std::vector<ChunkInfoStruct> ChunkManager::getChunksAsVector() const
{
    std::shared_lock lock(mutex_);
    std::vector<ChunkInfoStruct> chunksVector;
    for (const auto& [key, value] : chunks_) {
        chunksVector.push_back(value);
    }
    return chunksVector;
}

ChunkInfoStruct ChunkManager::getChunkById(int chunkId) const
{
    std::shared_lock lock(mutex_);
    if (chunks_.find(chunkId) != chunks_.end()) {
        return chunks_.at(chunkId);
    }
    return ChunkInfoStruct();
}

ChunkInfoStruct ChunkManager::getChunkByIP(int chunkId) const
{
    std::shared_lock lock(mutex_);
    for (const auto& [key, value] : chunks_) {
        if (value.id == chunkId) {
            return value;
        }
    }
    return ChunkInfoStruct();
}