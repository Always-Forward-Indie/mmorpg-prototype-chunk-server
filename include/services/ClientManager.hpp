#pragma once

#include <iostream>
#include <vector>
#include "data/DataStructs.hpp"
#include <utils/Logger.hpp>
#include <shared_mutex>

class ClientManager {
public:
    // Constructor
    ClientManager(Logger& logger);

    // Load clients list
    void loadClientsList(std::vector<ClientDataStruct> clientsList);

    // Load client data
    void loadClientData(ClientDataStruct clientData);

    // Set client socket
    void setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // Get clients list
    std::vector<ClientDataStruct> getClientsList();

    // Get basic client data by client ID
    ClientDataStruct getClientData(int clientID);

    // Get Client Socket by client ID
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket(int clientID);


    // remove client by ID
    void removeClientData(int clientID);

    // remove client by socket
    void removeClientDataBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket);


private:

    Logger& logger_;
    // clients list
    std::vector<ClientDataStruct> clientsList_;

    // Mutex for clients list
    std::shared_mutex mutex_;
};