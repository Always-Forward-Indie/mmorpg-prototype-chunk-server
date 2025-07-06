#pragma once

#include "data/DataStructs.hpp"
#include <iostream>
#include <shared_mutex>
#include <unordered_map>
#include <utils/Logger.hpp>
#include <vector>

class ClientManager
{
  public:
    // Constructor
    ClientManager(Logger &logger);

    // Load clients list
    void loadClientsList(std::vector<ClientDataStruct> clientsList);

    // Load client data
    void loadClientData(ClientDataStruct clientData);

    // Set client socket
    void setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // Get clients list
    std::vector<ClientDataStruct> getClientsList();

    // Get clients list without cleanup (thread-safe read-only)
    std::vector<ClientDataStruct> getClientsListReadOnly();

    // Get basic client data by client ID
    ClientDataStruct getClientData(int clientID);

    // Get Client Socket by client ID
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket(int clientID);

    // Get client ID by socket
    int getClientIdBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // remove client by ID
    void removeClientData(int clientID);

    // remove client by socket
    void removeClientDataBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // cleanup clients with invalid sockets
    void cleanupInvalidClients();

    // Force cleanup of all disconnected clients and shrink containers to prevent memory leaks
    void forceCleanupMemory();

  private:
    Logger &logger_;
    // clients list
    std::vector<ClientDataStruct> clientsList_;
    // socket map - tracks socket references separately from client data
    std::unordered_map<int, std::shared_ptr<boost::asio::ip::tcp::socket>> clientSockets_;

    // Mutex for clients list
    std::shared_mutex mutex_;
};