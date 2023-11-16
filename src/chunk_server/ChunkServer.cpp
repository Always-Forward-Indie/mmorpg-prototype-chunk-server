#include "chunk_server/ChunkServer.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

ChunkServer::ChunkServer(boost::asio::io_context &io_context, const std::string &customIP, short customPort, short maxClients)
    : io_context_(io_context),
      acceptor_(io_context),
      clientData_(),
      authenticator_(),
      characterManager_(),
      gameServerWorker_()
{
    boost::system::error_code ec;

    // Create an endpoint with the custom IP and port
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);

    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }

    if (ec)
    {
        std::cerr << RED << "Error during server initialization: " << ec.message() << RESET << std::endl;
        return;
    }

    startAccept();

    // Print IP address and port when the server starts
    std::cout << GREEN << "Chunk Server started on IP: " << customIP << ", Port: " << customPort << RESET << std::endl;

    // Start the main event loop in a new thread
    std::thread eventLoopThread(&ChunkServer::mainEventLoop, this);
    eventLoopThread.detach(); // Or manage the thread more carefully based on your application's needs
}

void ChunkServer::startAccept()
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error)
                           {
        if (!error) {
            // Get the client's remote endpoint (IP address and port)
            boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
            std::string clientIP = remoteEndpoint.address().to_string();

            // Print the client's IP address
            std::cout << GREEN << "New client with IP: " << clientIP << " connected!" << RESET << std::endl;

            // Start reading data from the client
            startReadingFromClient(clientSocket);
        }

        // Continue accepting new connections even if there's an error
        startAccept(); });
}

void ChunkServer::handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length> &dataBuffer, size_t bytes_transferred)
{
    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(dataBuffer.data(), dataBuffer.data() + bytes_transferred);
        // Create a JSON object for the response
        nlohmann::json response;

        // Extract hash, login, type and password fields from the jsonData
        std::string type = jsonData["type"] != nullptr ? jsonData["type"] : "";
        std::string hash = jsonData["hash"] != nullptr ? jsonData["hash"] : "";

        // Check if the type of request is joinGame
        if (type == "joinGame")
        {
            int character_id = jsonData["characterId"] != nullptr ? jsonData["characterId"].get<int>() : 0;
            int client_id = jsonData["clientId"] != nullptr ? jsonData["clientId"].get<int>() : 0;

            joinGame(clientSocket, hash, character_id, client_id);
        }
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::cerr << RED << "JSON parsing error: " << e.what() << RESET << std::endl;
        // Handle the error (e.g., close the socket)
    }
}

// TODO - Refactor this method 
void ChunkServer::joinGame(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &hash, const int &characterId, const int &clientId)
{
    // Authenticate the client
    int characterID = authenticator_.authenticate(clientData_, hash, clientId);
    // Create a JSON object for the response
    nlohmann::json response;

    // Define a callback function to handle the completion of the send operation
    auto sendToGameServerCallback = [](const boost::system::error_code &error, std::size_t bytes_transferred)
    {
        if (!error)
        {
            std::cout << GREEN << "Data sent successfully to the Game server." << RESET << std::endl;
        }
        else
        {
            std::cerr << RED << "Error sending data to the Game server: " << error.message() << RESET << std::endl;
        }
    };

    // Check if the authentication was successful
    if (characterID == 0)
    {
        // Add the message to the response
        response["message"] = "Authentication failed for user!";
        // Prepare a response message
        std::string responseData = generateResponseMessage("error", response, 0);
        // Send the response to the client
        sendResponse(clientSocket, responseData);
        return;
    }

    // Get the character data from the database
    CharacterDataStruct characterData = characterManager_.getCharacterData(clientData_, clientId, characterId);
    // Set the character data in the clientData_ object
    characterManager_.setCharacterData(clientData_, clientId, characterData);
    // Get the character position from the database
    PositionStruct characterPosition = characterManager_.getCharacterPosition(clientData_, clientId, characterId);
    // Set the character position in the clientData_ object
    characterManager_.setCharacterPosition(clientData_, clientId, characterPosition);

    // Get the current client data
    const ClientDataStruct *currentClientData = clientData_.getClientData(clientId);
    if(currentClientData == nullptr) {
        // Add the message to the response
        response["message"] = "Client data not found!";
        // Prepare a response message
        std::string responseData = generateResponseMessage("error", response, 0);
        // Send the response to the client
        sendResponse(clientSocket, responseData);
        return;
    }
    characterData = currentClientData->characterData;

    // Send data to the chunk server
    gameServerWorker_.sendDataToGameServer("Hello, Chunk Server!\n", sendToGameServerCallback);

    // Add the message to the response
    response["message"] = "Authentication success for user!";
    response["hash"] = currentClientData->hash;
    response["clientId"] = clientId;
    response["characterId"] = characterData.characterId;
    response["characterPosX"] = characterData.characterPosition.positionX;
    response["characterPosY"] = characterData.characterPosition.positionY;
    response["characterPosZ"] = characterData.characterPosition.positionZ;
    // Prepare a response message
    std::string responseData = generateResponseMessage("success", response, clientId);
    // Send the response to the client
    sendResponse(clientSocket, responseData);
}

void ChunkServer::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString),
                             [this, clientSocket](const boost::system::error_code &error, size_t bytes_transferred)
                             {
                                 if (!error)
                                 {
                                     // Response sent successfully, now start listening for the client's next message
                                     startReadingFromClient(clientSocket);
                                 }
                                 else
                                 {
                                     std::cerr << RED << "Error during async_write: " << error.message() << RESET << std::endl;
                                     // Handle the error (e.g., close the socket)
                                 }
                             });
}

void ChunkServer::startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    auto dataBuffer = std::make_shared<std::array<char, max_length>>();
    clientSocket->async_read_some(boost::asio::buffer(*dataBuffer),
                                  [this, clientSocket, dataBuffer](const boost::system::error_code &error, size_t bytes_transferred)
                                  {
                                      if (!error)
                                      {
                                          // Data has been read successfully, handle it
                                          handleClientData(clientSocket, *dataBuffer, bytes_transferred);

                                          // Continue reading from the client
                                          startReadingFromClient(clientSocket);
                                      }
                                      else if (error == boost::asio::error::eof)
                                      {
                                          // The client has closed the connection
                                          std::cerr << RED << "Client disconnected gracefully." << RESET << std::endl;

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else if (error == boost::asio::error::operation_aborted)
                                      {
                                          // The read operation was canceled, likely due to the client disconnecting
                                          std::cerr << RED << "Read operation canceled (client disconnected)." << RESET << std::endl;

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else
                                      {
                                          // Handle other errors
                                          std::cerr << RED << "Error during async_read_some: " << error.message() << RESET << std::endl;

                                          // You can also close the socket in case of other errors if needed
                                          clientSocket->close();
                                      }
                                  });
}

std::string ChunkServer::generateResponseMessage(const std::string &status, const nlohmann::json &message, const int &id)
{
    nlohmann::json response;

    response["status"] = status;
    response["body"] = message;

    std::string responseString = response.dump();

    std::cout << YELLOW << "Client data: " << responseString << RESET << std::endl;

    return responseString;
}

void ChunkServer::onPlayerMoveReceived(const int &clientId, float x, float y, float z) {
    // Create a PositionStruct object with the new position
    PositionStruct characterPosition;
    characterPosition.positionX = x;
    characterPosition.positionY = y;
    characterPosition.positionZ = z;
    // Create a new event and push it to the queue
    Event moveEvent(Event::MOVE, clientId, characterPosition);
    _eventQueue.push(moveEvent);
}

void ChunkServer::mainEventLoop() {
    std::cout << BLUE << "Waiting for Events..." << RESET << std::endl;

    while (true) {
        Event event;
        if (_eventQueue.pop(event)) {
            _eventHandler.dispatchEvent(event, clientData_);
        }

        // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}