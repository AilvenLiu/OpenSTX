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

#include <iostream>
#include <filesystem>
#include <memory>

#include "Logger.h"
#include "RealTimeData.h"
#include "TimescaleDB.h"

int main() {
    // Specify the log directory and ensure it exists
    std::string logDir = "logs";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
        std::cout << "Created directory: " << logDir << std::endl;
    }
    
    // Generate a timestamp for the log file name
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
    std::string timestamp = oss.str();

    std::string logFilePath = "logs/realtime_data_" + timestamp + ".log";
    std::shared_ptr<Logger> logger = std::make_shared<Logger>(logFilePath);
    STX_LOGI(logger, "Start main");

    // Initialize the TimescaleDB connection
    std::shared_ptr<TimescaleDB> timescaleDB = std::make_shared<TimescaleDB>(logger, "openstx", "openstx", "test_password", "localhost", "5432");

    // Initialize RealTimeData with logger and TimescaleDB
    std::shared_ptr<RealTimeData> dataCollector = std::make_shared<RealTimeData>(logger, timescaleDB);

    dataCollector->start();

    return 0;
}