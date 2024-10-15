// ZmqLogger.cpp
#include "ZmqLogger.h"
#include <iostream>
#include <iomanip>
#include <ctime>

using namespace openshot;

ZmqLogger* ZmqLogger::m_pInstance = nullptr;

ZmqLogger* ZmqLogger::Instance() {
    if (!m_pInstance) {
        m_pInstance = new ZmqLogger;
    }
    return m_pInstance;
}

void ZmqLogger::Path(std::string new_path) {
    const std::lock_guard<std::recursive_mutex> lock(loggerMutex);
    file_path = new_path;
    if (log_file.is_open()) {
        log_file.close();
    }
    log_file.open(file_path.c_str(), std::ios::out | std::ios::app);

    std::time_t now = std::time(0);
    std::tm* localtm = std::localtime(&now);
    log_file << "------------------------------------------" << std::endl;
    log_file << "libopenshot logging: " << std::asctime(localtm);
    log_file << "------------------------------------------" << std::endl;
}

void ZmqLogger::Log(std::string message) {
    if (!enabled) return;

    const std::lock_guard<std::recursive_mutex> lock(loggerMutex);
    std::cout << message; // Output to console
    LogToFile(message);
}

void ZmqLogger::LogToFile(std::string message) {
    if (log_file.is_open()) {
        log_file << message << std::flush;
    }
}

void ZmqLogger::Close() {
    enabled = false;
    if (log_file.is_open()) {
        log_file.close();
    }
}

void ZmqLogger::AppendDebugMethod(std::string method_name,
                                  std::string arg1_name, float arg1_value,
                                  std::string arg2_name, float arg2_value,
                                  std::string arg3_name, float arg3_value,
                                  std::string arg4_name, float arg4_value,
                                  std::string arg5_name, float arg5_value,
                                  std::string arg6_name, float arg6_value) {
    if (!enabled) return;

    const std::lock_guard<std::recursive_mutex> lock(loggerMutex);
    std::stringstream message;
    message << std::fixed << std::setprecision(4);

    message << method_name << " (";
    if (!arg1_name.empty()) message << arg1_name << "=" << arg1_value;
    if (!arg2_name.empty()) message << ", " << arg2_name << "=" << arg2_value;
    if (!arg3_name.empty()) message << ", " << arg3_name << "=" << arg3_value;
    if (!arg4_name.empty()) message << ", " << arg4_name << "=" << arg4_value;
    if (!arg5_name.empty()) message << ", " << arg5_name << "=" << arg5_value;
    if (!arg6_name.empty()) message << ", " << arg6_name << "=" << arg6_value;
    message << ")" << std::endl;

    Log(message.str());
}