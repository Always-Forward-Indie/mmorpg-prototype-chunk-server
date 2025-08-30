#pragma once

#include "data/DataStructs.hpp"
#include <chrono>
#include <nlohmann/json.hpp>

/**
 * @brief Utility class for handling lag compensation timestamps
 */
class TimestampUtils
{
  public:
    /**
     * @brief Get current timestamp in milliseconds since epoch
     * @return Current timestamp in milliseconds
     */
    static long long getCurrentTimestampMs();

    /**
     * @brief Create timestamp struct with serverRecvMs set to current time
     * @param clientSendMsEcho Echo timestamp from client request (0 if not available)
     * @param requestId Echo of client requestId for packet synchronization
     * @return TimestampStruct with serverRecvMs set and client data echoed
     */
    static TimestampStruct createReceiveTimestamp(long long clientSendMsEcho = 0, const std::string &requestId = "");

    /**
     * @brief Update timestamp struct with serverSendMs set to current time
     * @param timestamps Reference to timestamp struct to update
     */
    static void setServerSendTimestamp(TimestampStruct &timestamps);

    /**
     * @brief Extract client timestamp from JSON request
     * @param requestJson JSON request from client
     * @return Client timestamp in milliseconds or 0 if not found
     */
    static long long extractClientTimestamp(const nlohmann::json &requestJson);

    /**
     * @brief Extract client requestId from JSON request
     * @param requestJson JSON request from client
     * @return Client requestId string or empty string if not found
     */
    static std::string extractRequestId(const nlohmann::json &requestJson);

    /**
     * @brief Add timestamps to JSON response
     * @param responseJson JSON response to modify
     * @param timestamps Timestamp struct to add
     */
    static void addTimestampsToResponse(nlohmann::json &responseJson, const TimestampStruct &timestamps);

    /**
     * @brief Add timestamps to response header
     * @param responseJson JSON response to modify
     * @param timestamps Timestamp struct to add
     */
    static void addTimestampsToHeader(nlohmann::json &responseJson, const TimestampStruct &timestamps);

    /**
     * @brief Create complete timestamp struct for response
     * @param clientSendMsEcho Echo timestamp from client request
     * @param serverRecvMs When server received the packet
     * @param requestId Echo of client requestId for packet synchronization
     * @return Complete timestamp struct with serverSendMs set to current time
     */
    static TimestampStruct createResponseTimestamp(long long clientSendMsEcho, long long serverRecvMs, const std::string &requestId = "");

    /**
     * @brief Parse timestamps from JSON request
     * @param requestJson JSON request from client
     * @return TimestampStruct with parsed data
     */
    static TimestampStruct parseTimestampsFromRequest(const nlohmann::json &requestJson);
};
