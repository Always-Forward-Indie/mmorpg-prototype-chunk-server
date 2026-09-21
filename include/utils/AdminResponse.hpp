#pragma once
// Shared admin-RPC response envelope (DEV test hook).
//
// One shape for every adminCommand answer, whether it is sent from the
// dispatcher fast path (sync manager ops) or from a queued event handler:
// header.eventType = header.event = "adminCommand" (the python harness
// matches on eventType via wait_for), status success|error, \n-terminated.
#include "data/DataStructs.hpp"
#include "network/NetworkManager.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

inline void sendAdminResponse(NetworkManager &networkManager,
    const std::shared_ptr<boost::asio::ip::tcp::socket> &socket,
    int callerClientId,
    const TimestampStruct &timestamps,
    const std::string &status,
    const std::string &op,
    const nlohmann::json &body,
    const std::string &message)
{
    if (!socket)
        return;
    try
    {
        if (!socket->is_open())
            return;
    }
    catch (...)
    {
        return;
    }
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                           .count();
    nlohmann::json rsp;
    rsp["header"]["eventType"] = "adminCommand";
    rsp["header"]["event"] = "adminCommand";
    rsp["header"]["clientId"] = callerClientId;
    rsp["header"]["status"] = status;
    rsp["header"]["message"] = message;
    rsp["header"]["op"] = op;
    rsp["header"]["serverRecvMs"] =
        timestamps.serverRecvMs != 0 ? timestamps.serverRecvMs : nowMs;
    rsp["header"]["serverSendMs"] = nowMs;
    rsp["header"]["clientSendMsEcho"] = timestamps.clientSendMsEcho;
    rsp["header"]["requestId"] = timestamps.requestId;
    rsp["header"]["version"] = "1.0";
    rsp["body"] = body;
    auto data = std::make_shared<const std::string>(rsp.dump() + "\n");
    networkManager.sendResponse(socket, data);
}
