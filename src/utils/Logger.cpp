#include "utils/Logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

Logger::Logger() : stopThread_(false), loggingThread_(&Logger::processQueue, this) {
}

Logger::~Logger() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopThread_ = true;
    }
    cv_.notify_all();
    if (loggingThread_.joinable()) {
        loggingThread_.join();
    }
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    {
        std::lock_guard<std::mutex> lock(timeMutex);
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    }
    return ss.str();
}

void Logger::log(const std::string& message, const std::string& color) {
    LogMessage logMsg;
    logMsg.type = "INFO";
    logMsg.timestamp = getCurrentTimestamp();
    logMsg.message = message;
    logMsg.color = color;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        logQueue_.push(logMsg);
    }
    cv_.notify_one();
}

void Logger::logError(const std::string& message, const std::string& color) {
    LogMessage logMsg;
    logMsg.type = "ERROR";
    logMsg.timestamp = getCurrentTimestamp();
    logMsg.message = message;
    logMsg.color = color;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        logQueue_.push(logMsg);
    }
    cv_.notify_one();
}

void Logger::processQueue() {
    while (true) {
        LogMessage logMsg;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this]() { return !logQueue_.empty() || stopThread_; });
            if (stopThread_ && logQueue_.empty())
                break;
            logMsg = std::move(logQueue_.front());
            logQueue_.pop();
        }
        if (logMsg.type == "INFO") {
            std::cout << logMsg.color << "[" << logMsg.type << "] [" 
                      << logMsg.timestamp << "] " << logMsg.message << RESET << std::endl;
        } else if (logMsg.type == "ERROR") {
            std::cerr << logMsg.color << "[" << logMsg.type << "] [" 
                      << logMsg.timestamp << "] " << logMsg.message << RESET << std::endl;
        }
    }
}
