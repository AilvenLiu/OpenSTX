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

#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <ctime>

// Define log levels
enum LogLevel {
    FATAL,
    ERROR,
    WARNING,
    INFO,
    DEBUG
};

class Logger {
private:
    std::ofstream logFile;
    std::mutex logMutex;
    LogLevel logLevel;

    std::string getTimestamp() const;

public:
    Logger(const std::string& filename, LogLevel level = INFO);
    ~Logger();

    void log(LogLevel level, const std::string& message, const char* file, int line, const char* func);
    void setLogLevel(LogLevel level);

    static std::string logLevelToString(LogLevel level);
    static LogLevel stringToLogLevel(const std::string& levelStr);
};

// Define log macros
#define STX_LOGF(logger, message) (logger)->log(LogLevel::FATAL, message, __FILE__, __LINE__, __func__)
#define STX_LOGE(logger, message) (logger)->log(LogLevel::ERROR, message, __FILE__, __LINE__, __func__)
#define STX_LOGW(logger, message) (logger)->log(LogLevel::WARNING, message, __FILE__, __LINE__, __func__)
#define STX_LOGI(logger, message) (logger)->log(LogLevel::INFO, message, __FILE__, __LINE__, __func__)
#define STX_LOGD(logger, message) (logger)->log(LogLevel::DEBUG, message, __FILE__, __LINE__, __func__)

#endif // LOGGER_H