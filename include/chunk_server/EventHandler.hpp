#include "Event.hpp"
#include <string>

class EventHandler {
public:
  void dispatchEvent(const Event& event, ClientData& clientData);

private:
    void handleMoveEvent(const Event& event, ClientData& clientData);
    void handleInteractEvent(const Event& event, ClientData& clientData);
    // Other private handler methods
};