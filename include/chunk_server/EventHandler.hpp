#include "Event.hpp"

class EventHandler {
public:
  void dispatchEvent(const Event& event);

private:
    void handleMoveEvent(const Event& event);
    void handleInteractEvent(const Event& event);
    // Other private handler methods
};