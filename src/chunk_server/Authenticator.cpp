#include "chunk_server/Authenticator.hpp"
#include "helpers/Database.hpp"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>
#include <pqxx/pqxx>
#include <iostream>


// Add these using declarations for convenience
using namespace pqxx;
using namespace std;

int Authenticator::authenticate(Database& database, ClientData& clientData, const std::string& hash, const int& user_id) {
    try {
        CharacterDataStruct characterDataStruct;
        ClientDataStruct clientDataStruct;

        // Create a PostgreSQL database connection
        pqxx::work transaction(database.getConnection());
        pqxx::result getUserDBData = database.executeQueryWithTransaction(
                    transaction,
                    "search_user",
                    {hash});

        if (!getUserDBData.empty()) {
            clientDataStruct.clientId = user_id;
            clientDataStruct.hash = hash;

            if(user_id == getUserDBData[0][0].as<int>()){
                transaction.commit(); // Commit the transaction
            }

            clientData.storeClientData(clientDataStruct);  // Store clientData in the ClientData class

            return user_id;
        } else {
            // User not found message
            std::cerr << "User with ID: " << user_id << " not found" << std::endl;
            // Authentication failed, return false
            transaction.abort(); // Rollback the transaction (optional)
            return 0;
        }
    } catch (const std::exception& e) {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return 0;
    }
}