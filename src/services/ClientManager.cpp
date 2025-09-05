#include "services/ClientManager.hpp"

ClientManager::ClientManager(Logger &logger)
    : logger_(logger)
{
}

void
ClientManager::loadClientsList(std::vector<ClientDataStruct> clientsList)
{
    try
    {
        if (clientsList.empty())
        {
            logger_.logError("No clients found in the GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : clientsList)
        {
            clientsList_.push_back(row);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading clients: " + std::string(e.what()));
    }
}

void
ClientManager::loadClientData(ClientDataStruct clientData)
{
    try
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        // Check if client already exists to avoid duplicates
        for (auto &existingClient : clientsList_)
        {
            if (existingClient.clientId == clientData.clientId)
            {
                // Update existing client data, preserve characterId if already set
                if (existingClient.characterId == 0 && clientData.characterId != 0)
                {
                    existingClient.characterId = clientData.characterId;
                }
                existingClient.hash = clientData.hash;
                logger_.log("Updated existing client ID: " + std::to_string(clientData.clientId));
                return;
            }
        }

        // Client doesn't exist, add new one
        clientsList_.push_back(clientData);
        logger_.log("Added new client ID: " + std::to_string(clientData.clientId));
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading client data: " + std::string(e.what()));
    }
}

std::vector<ClientDataStruct>
ClientManager::getClientsList()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Clean up invalid clients first by checking the socket map
    for (auto it = clientsList_.begin(); it != clientsList_.end();)
    {
        bool shouldRemove = false;
        auto socketIt = clientSockets_.find(it->clientId);

        if (socketIt == clientSockets_.end() || !socketIt->second)
        {
            shouldRemove = true;
        }
        else
        {
            try
            {
                if (!socketIt->second->is_open())
                {
                    shouldRemove = true;
                }
            }
            catch (const std::exception &e)
            {
                // Socket is likely invalid/freed
                shouldRemove = true;
            }
        }

        if (shouldRemove)
        {
            logger_.log("Removing client with invalid socket during getClientsList, ID: " + std::to_string(it->clientId));
            // Remove from both the list and socket map
            clientSockets_.erase(it->clientId);
            it = clientsList_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Return a copy of the client list (without socket references)
    return clientsList_;
}

std::vector<ClientDataStruct>
ClientManager::getClientsListReadOnly()
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return clientsList_; // Thread-safe read-only copy
}

ClientDataStruct
ClientManager::getClientData(int clientID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            return client;
        }
    }
    return ClientDataStruct();
}

std::shared_ptr<boost::asio::ip::tcp::socket>
ClientManager::getClientSocket(int clientID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = clientSockets_.find(clientID);
    if (it != clientSockets_.end())
    {
        return it->second;
    }
    return nullptr;
}

int
ClientManager::getClientIdBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &pair : clientSockets_)
    {
        if (pair.second == socket)
        {
            return pair.first;
        }
    }
    return 0; // Return 0 if socket not found (0 means invalid client ID)
}

void
ClientManager::setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    clientSockets_[clientID] = socket;
}

// Set client character ID
void
ClientManager::setClientCharacterId(int clientID, int characterId)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            client.characterId = characterId;
            logger_.log("Set character ID " + std::to_string(characterId) + " for client ID: " + std::to_string(clientID));
            return;
        }
    }

    // Client not found, create a minimal client entry
    logger_.log("Client ID " + std::to_string(clientID) + " not found, creating minimal client entry for character ID: " + std::to_string(characterId));
    ClientDataStruct newClient;
    newClient.clientId = clientID;
    newClient.characterId = characterId;
    clientsList_.push_back(newClient);
    logger_.log("Created and set character ID " + std::to_string(characterId) + " for new client ID: " + std::to_string(clientID));
}

// remove client by ID
void
ClientManager::removeClientData(int clientID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Remove from client list
    for (auto it = clientsList_.begin(); it != clientsList_.end(); ++it)
    {
        if (it->clientId == clientID)
        {
            clientsList_.erase(it);
            break;
        }
    }

    // Remove from socket map
    clientSockets_.erase(clientID);
}

void
ClientManager::removeClientDataBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Find the client ID that corresponds to this socket
    int clientIDToRemove = -1;
    for (const auto &pair : clientSockets_)
    {
        if (pair.second == socket)
        {
            clientIDToRemove = pair.first;
            break;
        }
    }

    // Remove both from client list and socket map
    if (clientIDToRemove != -1)
    {
        for (auto it = clientsList_.begin(); it != clientsList_.end(); ++it)
        {
            if (it->clientId == clientIDToRemove)
            {
                clientsList_.erase(it);
                break;
            }
        }
        clientSockets_.erase(clientIDToRemove);
    }
}

void
ClientManager::cleanupInvalidClients()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = clientsList_.begin(); it != clientsList_.end();)
    {
        bool shouldRemove = false;
        auto socketIt = clientSockets_.find(it->clientId);

        if (socketIt == clientSockets_.end() || !socketIt->second)
        {
            shouldRemove = true;
        }
        else
        {
            try
            {
                if (!socketIt->second->is_open())
                {
                    shouldRemove = true;
                }
            }
            catch (const std::exception &e)
            {
                // Socket is likely invalid/freed
                shouldRemove = true;
            }
        }

        if (shouldRemove)
        {
            logger_.log("Removing client with invalid socket, ID: " + std::to_string(it->clientId));
            // Remove from both the list and socket map
            clientSockets_.erase(it->clientId);
            it = clientsList_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// Force cleanup of all disconnected clients and shrink containers
void
ClientManager::forceCleanupMemory()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    size_t initialListSize = clientsList_.size();
    size_t initialSocketsSize = clientSockets_.size();

    // Remove all clients with invalid or closed sockets
    for (auto it = clientsList_.begin(); it != clientsList_.end();)
    {
        bool shouldRemove = false;
        auto socketIt = clientSockets_.find(it->clientId);

        if (socketIt == clientSockets_.end() || !socketIt->second)
        {
            shouldRemove = true;
        }
        else
        {
            try
            {
                if (!socketIt->second->is_open())
                {
                    shouldRemove = true;
                }
            }
            catch (const std::exception &e)
            {
                shouldRemove = true;
            }
        }

        if (shouldRemove)
        {
            logger_.log("Force cleanup removing client ID: " + std::to_string(it->clientId));
            clientSockets_.erase(it->clientId);
            it = clientsList_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Shrink containers if they've grown too large
    if (clientsList_.capacity() > clientsList_.size() * 2 && clientsList_.capacity() > 100)
    {
        clientsList_.shrink_to_fit();
        logger_.log("Shrunk clientsList capacity to reduce memory usage");
    }

    size_t finalListSize = clientsList_.size();
    size_t finalSocketsSize = clientSockets_.size();

    if (initialListSize > finalListSize || initialSocketsSize > finalSocketsSize)
    {
        logger_.log("Memory cleanup: removed " + std::to_string(initialListSize - finalListSize) +
                    " clients and " + std::to_string(initialSocketsSize - finalSocketsSize) + " sockets");
    }
}
