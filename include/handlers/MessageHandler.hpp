#pragma once

#include "utils/JSONParser.hpp"

class MessageHandler
{
  public:
    MessageHandler(JSONParser &jsonParser);

    // Single-parse path: takes the already-parsed wire JSON (see
    // ClientSession::processMessage). NOTE: parseMessage(string) was removed
    // as dead code (no callers); do not reintroduce string-based parsing.
    std::tuple<std::string, ClientDataStruct, CharacterDataStruct, PositionStruct, MessageStruct, TimestampStruct>
    parseMessageWithTimestamps(const nlohmann::json &jsonData);

  private:
    JSONParser &jsonParser_;
};