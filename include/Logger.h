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

// 定义日志级别的枚举
enum class LogLevel {
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
    Logger(const std::string& filename, LogLevel level = LogLevel::INFO);
    ~Logger();

    void log(LogLevel level, const std::string& message);

    static std::string logLevelToString(LogLevel level);
};

// 定义日志宏
#define STX_LOGF(logger, message) (logger)->log(LogLevel::FATAL, message)
#define STX_LOGE(logger, message) (logger)->log(LogLevel::ERROR, message)
#define STX_LOGW(logger, message) (logger)->log(LogLevel::WARNING, message)
#define STX_LOGI(logger, message) (logger)->log(LogLevel::INFO, message)
#define STX_LOGD(logger, message) (logger)->log(LogLevel::DEBUG, message)

#endif // LOGGER_H