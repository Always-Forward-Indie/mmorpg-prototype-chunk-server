#include "services/ClientManager.hpp"

ClientManager::ClientManager(Logger& logger)
    : logger_(logger)
{
}

void ClientManager::loadClientsList(std::vector<ClientDataStruct> clientsList)
{
    try
    {
        if (clientsList.empty())
        {
            logger_.logError("No clients found in the GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto& row : clientsList)
        {
            clientsList_.push_back(row);
        }
    }
    catch (const std::exception& e)
    {
        logger_.logError("Error loading clients: " + std::string(e.what()));
    }
}

void ClientManager::loadClientData(ClientDataStruct clientData)
{
    try
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        clientsList_.push_back(clientData);
    }
    catch (const std::exception& e)
    {
        logger_.logError("Error loading client data: " + std::string(e.what()));
    }
}

std::vector<ClientDataStruct> ClientManager::getClientsList()
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return clientsList_;
}

ClientDataStruct ClientManager::getClientData(int clientID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            return client;
        }
    }
    return ClientDataStruct();
}

std::shared_ptr<boost::asio::ip::tcp::socket> ClientManager::getClientSocket(int clientID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            return client.socket;
        }
    }
    return nullptr;
}

void ClientManager::setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            client.socket = socket;
        }
    }
}

void ClientManager::removeClientData(int clientID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto it = clientsList_.begin(); it != clientsList_.end(); ++it)
    {
        if (it->clientId == clientID)
        {
            clientsList_.erase(it);
            break;
        }
    }
}

void ClientManager::removeClientDataBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto it = clientsList_.begin(); it != clientsList_.end(); ++it)
    {
        if (it->socket == socket)
        {
            clientsList_.erase(it);
            break;
        }
    }
}
