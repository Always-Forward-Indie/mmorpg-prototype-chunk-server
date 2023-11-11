#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <pqxx/pqxx>
#include <memory>
#include "Config.hpp"

class Database {
public:
    // Constructor
    Database();

    // Establish a database connection
    void connect();

    // Prepare default queries
    void prepareDefaultQueries();

    // Get a reference to the database connection
    pqxx::connection& getConnection();
    // Handle database connection or query errors
    void handleDatabaseError(const std::exception &e);
    // Execute a query with a transaction
    using ParamType = std::variant<int, float, double, std::string>; // Define a type of data alias for the parameter type
    pqxx::result executeQueryWithTransaction(
    pqxx::work &transaction,
    const std::string &preparedQueryName,
    const std::vector<ParamType> &parameters);

private:
    // Database connection
    std::unique_ptr<pqxx::connection> connection_;
};

#endif // DATABASE_HPP
