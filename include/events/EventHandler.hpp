#include "Event.hpp"
#include <string>
#include <boost/asio.hpp>
#include "data/ClientData.hpp"
#include "network/NetworkManager.hpp"
#include "utils/ResponseBuilder.hpp"

class EventHandler {
public:
  EventHandler(NetworkManager& networkManager, Logger& logger);
  void dispatchEvent(const Event& event, ClientData& clientData);

private:
    void handleJoinGameEvent(const Event& event, ClientData& clientData);
    void handleGetConnectedCharactersEvent(const Event &event, ClientData &clientData);
    void handleMoveEvent(const Event& event, ClientData& clientData);
    void handleInteractEvent(const Event& event, ClientData& clientData);
    void handleDisconnectClientEvent(const Event& event, ClientData& clientData);
    void handlePingClientEvent(const Event& event, ClientData& clientData);

    NetworkManager& networkManager_;
    Logger& logger_;
};