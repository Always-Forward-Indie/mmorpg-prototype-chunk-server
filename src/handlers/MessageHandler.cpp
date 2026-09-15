#include "handlers/MessageHandler.hpp"
#include "utils/TimestampUtils.hpp"

MessageHandler::MessageHandler(JSONParser &jsonParser) : jsonParser_(jsonParser) {}

std::tuple<std::string, ClientDataStruct, CharacterDataStruct, PositionStruct, MessageStruct, TimestampStruct>
MessageHandler::parseMessageWithTimestamps(const nlohmann::json &jsonData)
{
    // Single-parse path: the caller (ClientSession) already parsed the wire
    // bytes once. Field extractors below do zero additional parsing.
    std::string eventType = jsonParser_.parseEventType(jsonData);
    ClientDataStruct clientData = jsonParser_.parseClientData(jsonData);
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(jsonData);
    PositionStruct positionData = jsonParser_.parsePositionData(jsonData);
    MessageStruct messageStruct = jsonParser_.parseMessage(jsonData);

    // Parse timestamps and create receive timestamp with current server time
    TimestampStruct parsedTimestamps = jsonParser_.parseTimestamps(jsonData);
    std::string requestId = jsonParser_.parseRequestId(jsonData);
    TimestampStruct serverTimestamps = TimestampUtils::createReceiveTimestamp(parsedTimestamps.clientSendMsEcho, requestId);

    return {eventType, clientData, characterData, positionData, messageStruct, serverTimestamps};
}