#pragma once
#include <nlohmann/json.hpp>
#include <string>

class ResponseBuilder {
private:
    nlohmann::json response = {{"header", nlohmann::json::object()}, {"body", nlohmann::json::object()}};

public:
    ResponseBuilder() {
        response["header"] = nlohmann::json::object(); // Ensure "header" is a JSON object
        response["body"] = nlohmann::json::object();   // Ensure "body" is a JSON object
    }

    template<typename T>
    ResponseBuilder& setHeader(const std::string& key, const T& value) {
        response["header"][key] = value;
        return *this;
    }
    
    template<typename T>
    ResponseBuilder& setBody(const std::string& key, const T& value) {
        response["body"][key] = value;
        return *this;
    }

    nlohmann::json build() {
        return response; // returns the built JSON object
    }
};