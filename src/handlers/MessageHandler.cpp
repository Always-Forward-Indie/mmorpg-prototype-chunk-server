#include "handlers/MessageHandler.hpp"

MessageHandler::MessageHandler(JSONParser &jsonParser) : jsonParser_(jsonParser) {}
std::tuple<std::string, ClientDataStruct, CharacterDataStruct, PositionStruct, MessageStruct>
MessageHandler::parseMessage(const std::string &message) {
    const char* data = message.data();
    size_t messageLength = message.size();

    std::string eventType = jsonParser_.parseEventType(data, messageLength);
    ClientDataStruct clientData = jsonParser_.parseClientData(data, messageLength);
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(data, messageLength);
    PositionStruct positionData = jsonParser_.parsePositionData(data, messageLength);
    MessageStruct messageStruct = jsonParser_.parseMessage(data, messageLength);

    return {eventType, clientData, characterData, positionData, messageStruct};
}