#include "chunk_server/Authenticator.hpp"
#include <string>
#include <pqxx/pqxx>
#include <iostream>

// Add these using declarations for convenience
using namespace pqxx;
using namespace std;

int Authenticator::authenticate(ClientData &clientData, const std::string &hash, const int &user_id)
{

    CharacterDataStruct characterDataStruct;
    ClientDataStruct clientDataStruct;

    if (user_id != 0)
    {
        clientDataStruct.clientId = user_id;
        clientDataStruct.hash = hash;
        clientData.storeClientData(clientDataStruct); // Store clientData in the ClientData class

        return user_id;
    }
    else
    {
        // User not found message
        std::cerr << "User with ID: " << user_id << " not found" << std::endl;
        // Authentication failed, return false
        return 0;
    }
}