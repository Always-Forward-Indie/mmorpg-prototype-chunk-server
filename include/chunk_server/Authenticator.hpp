// Authenticator.hpp
#pragma once

#include <string>
#include <boost/asio.hpp>
#include "chunk_server/ClientData.hpp"
#include "helpers/TerminalColors.hpp"

class Authenticator {
public:
    int authenticate(ClientData& clientData, const std::string& hash, const int& user_id);
};