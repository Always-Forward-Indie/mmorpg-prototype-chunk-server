#include "utils/TimestampUtils.hpp"

long long
TimestampUtils::getCurrentTimestampMs()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return millis;
}

TimestampStruct
TimestampUtils::createReceiveTimestamp(long long clientSendMsEcho, const std::string &requestId)
{
    TimestampStruct timestamps;
    timestamps.serverRecvMs = getCurrentTimestampMs();
    timestamps.clientSendMsEcho = clientSendMsEcho;
    timestamps.requestId = requestId;
    timestamps.serverSendMs = 0; // Will be set later when response is sent
    return timestamps;
}

void
TimestampUtils::setServerSendTimestamp(TimestampStruct &timestamps)
{
    timestamps.serverSendMs = getCurrentTimestampMs();
}

long long
TimestampUtils::extractClientTimestamp(const nlohmann::json &requestJson)
{
    try
    {
        // Try to find timestamp in header first
        if (requestJson.contains("header") && requestJson["header"].is_object())
        {
            const auto &header = requestJson["header"];
            if (header.contains("clientSendMs") && header["clientSendMs"].is_number())
            {
                return header["clientSendMs"].get<long long>();
            }
            if (header.contains("timestamp") && header["timestamp"].is_number())
            {
                return header["timestamp"].get<long long>();
            }
        }

        // Try to find in body as fallback
        if (requestJson.contains("body") && requestJson["body"].is_object())
        {
            const auto &body = requestJson["body"];
            if (body.contains("clientSendMs") && body["clientSendMs"].is_number())
            {
                return body["clientSendMs"].get<long long>();
            }
            if (body.contains("timestamp") && body["timestamp"].is_number())
            {
                return body["timestamp"].get<long long>();
            }
        }

        // Try to find in root level
        if (requestJson.contains("clientSendMs") && requestJson["clientSendMs"].is_number())
        {
            return requestJson["clientSendMs"].get<long long>();
        }
        if (requestJson.contains("timestamp") && requestJson["timestamp"].is_number())
        {
            return requestJson["timestamp"].get<long long>();
        }
    }
    catch (const std::exception &e)
    {
        // Silently return 0 if parsing fails
    }

    return 0; // No timestamp found
}

std::string
TimestampUtils::extractRequestId(const nlohmann::json &requestJson)
{
    try
    {
        // Try to find requestId in header first
        if (requestJson.contains("header") && requestJson["header"].is_object())
        {
            const auto &header = requestJson["header"];
            if (header.contains("requestId") && header["requestId"].is_string())
            {
                return header["requestId"].get<std::string>();
            }
        }

        // Try to find in body as fallback
        if (requestJson.contains("body") && requestJson["body"].is_object())
        {
            const auto &body = requestJson["body"];
            if (body.contains("requestId") && body["requestId"].is_string())
            {
                return body["requestId"].get<std::string>();
            }
        }

        // Try to find in root level
        if (requestJson.contains("requestId") && requestJson["requestId"].is_string())
        {
            return requestJson["requestId"].get<std::string>();
        }
    }
    catch (const std::exception &e)
    {
        // Silently return empty string if parsing fails
    }

    return ""; // No requestId found
}

void
TimestampUtils::addTimestampsToResponse(nlohmann::json &responseJson, const TimestampStruct &timestamps)
{
    if (!responseJson.contains("body"))
    {
        responseJson["body"] = nlohmann::json::object();
    }

    responseJson["body"]["serverRecvMs"] = timestamps.serverRecvMs;
    responseJson["body"]["serverSendMs"] = timestamps.serverSendMs;
    responseJson["body"]["clientSendMsEcho"] = timestamps.clientSendMsEcho;

    if (!timestamps.requestId.empty())
    {
        responseJson["body"]["requestIdEcho"] = timestamps.requestId;
    }
}

void
TimestampUtils::addTimestampsToHeader(nlohmann::json &responseJson, const TimestampStruct &timestamps)
{
    if (!responseJson.contains("header"))
    {
        responseJson["header"] = nlohmann::json::object();
    }

    responseJson["header"]["serverRecvMs"] = timestamps.serverRecvMs;
    responseJson["header"]["serverSendMs"] = timestamps.serverSendMs;
    responseJson["header"]["clientSendMsEcho"] = timestamps.clientSendMsEcho;

    if (!timestamps.requestId.empty())
    {
        responseJson["header"]["requestIdEcho"] = timestamps.requestId;
    }
}

TimestampStruct
TimestampUtils::createResponseTimestamp(long long clientSendMsEcho, long long serverRecvMs, const std::string &requestId)
{
    TimestampStruct timestamps;
    timestamps.serverRecvMs = serverRecvMs;
    timestamps.clientSendMsEcho = clientSendMsEcho;
    timestamps.requestId = requestId;
    timestamps.serverSendMs = getCurrentTimestampMs();
    return timestamps;
}

TimestampStruct
TimestampUtils::parseTimestampsFromRequest(const nlohmann::json &requestJson)
{
    TimestampStruct timestamps;
    long long clientTimestamp = extractClientTimestamp(requestJson);
    std::string requestId = extractRequestId(requestJson);
    timestamps = createReceiveTimestamp(clientTimestamp, requestId);
    return timestamps;
}
