#include "chunk_server/EventHandler.hpp"
#include "chunk_server/Event.hpp"
#include "chunk_server/ClientData.hpp"

void EventHandler::handleMoveEvent(const Event& event, ClientData& clientData) {
    //Here we will update the position of the character in the object and send it back to the game server

    //Retrieve the data from the event
    const auto& data = event.getData(); 
    int clientID = event.getClientID();
    PositionStruct characterPosition;
    //Extract movement data
    try {
        characterPosition = std::get<PositionStruct>(data);

    } catch (const std::bad_variant_access& ex) {
        // Handle the case where the data is not of type PositionStruct
        // This might be logging the error, throwing another exception, etc.
    }

    //Update the clientData object with the new position
    clientData.updateCharacterPositionData(clientID, characterPosition);

    //TODO - Send the updated object back to the game server
    
}

void EventHandler::handleInteractEvent(const Event& event, ClientData& clientData) {
    //Here we will update the interaction of the character in the object and send it back to the game server
    // TODO - Implement this method
}

void EventHandler::dispatchEvent(const Event& event, ClientData& clientData) {
    switch (event.getType()) {
        case Event::MOVE:
            handleMoveEvent(event, clientData);
            break;
        case Event::INTERACT:
            handleInteractEvent(event, clientData);
            break;
        // Other cases...
    }
}