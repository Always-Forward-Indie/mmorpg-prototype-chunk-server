#include "chunk_server/EventHandler.hpp"
#include "chunk_server/Event.hpp"

//TODO - Implement methods to handle events

void EventHandler::handleMoveEvent(const Event& event) {
    //Here we will update the position of the character in the object and send it back to the game server
}

void EventHandler::handleInteractEvent(const Event& event) {
    //Here we will update the interaction of the character in the object and send it back to the game server
}

void EventHandler::dispatchEvent(const Event& event) {
    switch (event.getType()) {
        case Event::MOVE:
            handleMoveEvent(event);
            break;
        case Event::INTERACT:
            handleInteractEvent(event);
            break;
        // Other cases...
    }
}