#pragma once
#include <iostream>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <ctime>
#include "utils/TerminalColors.hpp"
#include <condition_variable>
#include <thread>
#include <queue>

class Logger {
    public:
        Logger();
        ~Logger();
    
        std::string getCurrentTimestamp();
        // Добавляем значения по умолчанию для цвета:
        void log(const std::string& message, const std::string& color = BLUE);
        void logError(const std::string& message, const std::string& color = RED);
    
    private:
        // Структура для хранения лог-сообщения
        struct LogMessage {
            std::string type;       // "INFO" или "ERROR"
            std::string timestamp;
            std::string message;
            std::string color;
        };
    
        // Метод, который запускается в отдельном потоке и обрабатывает очередь логов
        void processQueue();
    
        std::mutex logger_mutex_; // Используется, например, для getCurrentTimestamp()
        std::mutex queue_mutex_;
        std::condition_variable cv_;
        std::queue<LogMessage> logQueue_;
        bool stopThread_;
        std::thread loggingThread_;
    
        std::mutex timeMutex;
    };