#include "Event.hpp"
#include <string>
#include <boost/asio.hpp>
#include "data/ClientData.hpp"
#include "network/NetworkManager.hpp"

class EventHandler {
public:
  EventHandler(NetworkManager& networkManager);
  void dispatchEvent(const Event& event, ClientData& clientData);

private:
    void handleJoinEvent(const Event& event, ClientData& clientData);
    void handleMoveEvent(const Event& event, ClientData& clientData);
    void handleInteractEvent(const Event& event, ClientData& clientData);

    NetworkManager& networkManager_;
    // Other private handler methods
};