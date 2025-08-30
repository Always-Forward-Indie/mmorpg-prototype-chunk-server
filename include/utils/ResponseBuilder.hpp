#pragma once
#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <string>

class ResponseBuilder
{
  private:
    nlohmann::json response = {{"header", nlohmann::json::object()}, {"body", nlohmann::json::object()}};
    TimestampStruct timestamps_;

  public:
    ResponseBuilder()
    {
        response["header"] = nlohmann::json::object(); // Ensure "header" is a JSON object
        response["body"] = nlohmann::json::object();   // Ensure "body" is a JSON object
    }

    template <typename T>
    ResponseBuilder &setHeader(const std::string &key, const T &value)
    {
        response["header"][key] = value;
        return *this;
    }

    template <typename T>
    ResponseBuilder &setBody(const std::string &key, const T &value)
    {
        response["body"][key] = value;
        return *this;
    }

    /**
     * @brief Set timestamps for lag compensation
     * @param timestamps Timestamp struct to use
     */
    ResponseBuilder &setTimestamps(const TimestampStruct &timestamps)
    {
        timestamps_ = timestamps;
        return *this;
    }

    nlohmann::json build()
    {
        // Add timestamps to header if they were set
        if (timestamps_.serverRecvMs != 0 || timestamps_.serverSendMs != 0 || timestamps_.clientSendMsEcho != 0)
        {
            response["header"]["serverRecvMs"] = timestamps_.serverRecvMs;
            response["header"]["serverSendMs"] = timestamps_.serverSendMs;
            response["header"]["clientSendMsEcho"] = timestamps_.clientSendMsEcho;
        }

        return response; // returns the built JSON object
    }
};