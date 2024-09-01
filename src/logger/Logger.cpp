/**************************************************************************
 * This file is part of the OpenSTX project.
 *
 * OpenSTX (Open Smart Trading eXpert) is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenSTX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenSTX. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ailven.LIU
 * Email: ailven.x.liu@gmail.com
 * Date: 2024
 *************************************************************************/

#include "Logger.hpp"

Logger::Logger(const std::string& filename, LogLevel level, LogLevel printLevel)
    : logLevel(level), printLevel(printLevel) {  // Initialize printLevel
    logFile.open(filename, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

std::string Logger::getTimestamp() const {
    std::time_t now = std::time(nullptr);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

void Logger::log(LogLevel level, const std::string& message, const char* file, int line, const char* func) {
    if (level >= logLevel && level <= printLevel) {  // Check against printLevel
        std::lock_guard<std::mutex> lock(logMutex);
        logFile << "[" << getTimestamp() << "] "
                << "[" << logLevelToString(level) << "] "
                << "[" << file << ":" << line << " - " << func << "] "
                << message << std::endl;
    }
}

std::string Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::FATAL: return "FATAL";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::INFO: return "INFO";
        case LogLevel::DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}