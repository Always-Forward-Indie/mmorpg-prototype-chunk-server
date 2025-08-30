#include "handlers/MessageHandler.hpp"
#include "utils/TimestampUtils.hpp"

MessageHandler::MessageHandler(JSONParser &jsonParser) : jsonParser_(jsonParser) {}

std::tuple<std::string, ClientDataStruct, CharacterDataStruct, PositionStruct, MessageStruct>
MessageHandler::parseMessage(const std::string &message)
{
    const char *data = message.data();
    size_t messageLength = message.size();

    std::string eventType = jsonParser_.parseEventType(data, messageLength);
    ClientDataStruct clientData = jsonParser_.parseClientData(data, messageLength);
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(data, messageLength);
    PositionStruct positionData = jsonParser_.parsePositionData(data, messageLength);
    MessageStruct messageStruct = jsonParser_.parseMessage(data, messageLength);

    return {eventType, clientData, characterData, positionData, messageStruct};
}

std::tuple<std::string, ClientDataStruct, CharacterDataStruct, PositionStruct, MessageStruct, TimestampStruct>
MessageHandler::parseMessageWithTimestamps(const std::string &message)
{
    const char *data = message.data();
    size_t messageLength = message.size();

    std::string eventType = jsonParser_.parseEventType(data, messageLength);
    ClientDataStruct clientData = jsonParser_.parseClientData(data, messageLength);
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(data, messageLength);
    PositionStruct positionData = jsonParser_.parsePositionData(data, messageLength);
    MessageStruct messageStruct = jsonParser_.parseMessage(data, messageLength);

    // Parse timestamps and create receive timestamp with current server time
    TimestampStruct parsedTimestamps = jsonParser_.parseTimestamps(data, messageLength);
    std::string requestId = jsonParser_.parseRequestId(data, messageLength);
    TimestampStruct serverTimestamps = TimestampUtils::createReceiveTimestamp(parsedTimestamps.clientSendMsEcho, requestId);

    return {eventType, clientData, characterData, positionData, messageStruct, serverTimestamps};
}