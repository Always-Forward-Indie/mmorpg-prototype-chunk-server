#include "helpers/Database.hpp"
#include "helpers/Config.hpp"
#include <iostream>

Database::Database()
{
    connect();
    prepareDefaultQueries();
}

void Database::connect()
{
    try
    {
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<0>(configs).port;
        std::string host = std::get<0>(configs).host;
        std::string databaseName = std::get<0>(configs).dbname;
        std::string user = std::get<0>(configs).user;
        std::string password = std::get<0>(configs).password;

        std::cout << "Connecting to database..." << std::endl;
        std::cout << "Database name: " << databaseName << std::endl;
        std::cout << "User: " << user << std::endl;
        std::cout << "Host: " << host << std::endl;
        std::cout << "Port: " << port << std::endl;

        connection_ = std::make_unique<pqxx::connection>(
            "dbname=" + databaseName + " user=" + user + " password=" + password + " hostaddr=" + host + " port=" + std::to_string(port));

        if (connection_->is_open())
        {
            std::cout << "Database connection established" << std::endl;
        }
        else
        {
            std::cerr << "Database connection failed" << std::endl;
            // Handle the connection failure (e.g., throw an exception or exit)
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error while connecting to the database: " << e.what() << std::endl;
        // Handle the exception (e.g., throw it or exit the application)
    }
}

void Database::prepareDefaultQueries()
{
    if (connection_->is_open())
    {
        connection_->prepare("search_user", "SELECT * FROM users WHERE session_key = $1 LIMIT 1;");

        connection_->prepare("get_character", "SELECT characters.id as character_id, characters.level as character_lvl, "
                                              "characters.name as character_name, character_class.name as character_class, race.name as race_name "
                                              "FROM characters "
                                              "JOIN character_class ON characters.class_id = character_class.id "
                                              "JOIN race on characters.race_id = race.id "
                                              "WHERE characters.owner_id = $1 AND characters.id = $2 LIMIT 1;");
        connection_->prepare("set_character_level", "UPDATE characters "
                                                    "SET level = $1 WHERE id = $2;");
        connection_->prepare("set_character_health", "UPDATE characters "
                                                     "SET current_health = $1 WHERE id = $2;");
        connection_->prepare("set_character_mana", "UPDATE characters "
                                                   "SET current_mana = $1 WHERE id = $2;");
        connection_->prepare("set_character_exp", "UPDATE characters "
                                                  "SET experience_points = $1 WHERE id = $2;");

        connection_->prepare("get_character_position", "SELECT x, y, z FROM character_position WHERE character_id = $1 LIMIT 1;");
        connection_->prepare("set_character_position", "UPDATE character_position SET x = $1, y = $2, z = $3 WHERE character_id = $4;");
    }
    else
    {
        std::cerr << "Cannot prepare queries: Database connection is not open." << std::endl;
        // Handle this situation (e.g., throw an exception or exit)
    }
}

pqxx::connection &Database::getConnection()
{
    if (connection_->is_open())
    {
        return *connection_;
    }
    else
    {
        throw std::runtime_error("Database connection is not open.");
    }
}

// Function to handle database errors
void Database::handleDatabaseError(const std::exception &e)
{
    // Handle database connection or query errors
    std::cerr << "Database error: " << e.what() << std::endl;
    // You might want to send an error response back to the client or log the error
}

// Function to execute a query with a transaction
using ParamType = std::variant<int, float, double, std::string>; // Define a type of data alias for the parameter type
pqxx::result Database::executeQueryWithTransaction(
    pqxx::work &transaction,
    const std::string &preparedQueryName,
    const std::vector<ParamType> &parameters)
{
 try
    {
        // Create a pqxx::params object to hold the parameters
        pqxx::params pq_params;

        // Loop through the parameters and add them to the pqxx::params object
        for (const auto &param : parameters)
        {
            std::visit([&](const auto &value) {
                pq_params.append(value);
            }, param);
        }

        // Execute the prepared query and assign the result to a pqxx::result object
        pqxx::result result = transaction.exec_prepared(preparedQueryName, pq_params);
        // Return the result
        return result;
    }
    catch (const std::exception &e)
    {
        transaction.abort(); // Rollback the transaction
        handleDatabaseError(e); // Handle database connection or query errors
        return pqxx::result(); // Return an empty result
    }
}