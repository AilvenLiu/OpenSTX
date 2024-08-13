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


#include <signal.h>
#include <iostream>
#include <filesystem>
#include <memory>

#include "Config.h"
#include "Logger.h"
#include "RealTimeData.h"
#include "TimescaleDB.h"

bool running = true;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    running = false;
}

int main() {
    signal(SIGINT, signalHandler);

    std::string logDir = "logs";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
        std::cout << "Created directory: " << logDir << std::endl;
    }
    
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d-%H:%M:%S");
    std::string timestamp = oss.str();

    std::string logFilePath = "logs/realtime_data_" + timestamp + ".log";
    std::shared_ptr<Logger> logger = std::make_shared<Logger>(logFilePath);
    STX_LOGI(logger, "Start main");

    // connect to timescale database
    std::string configFilePath = "conf/alicloud_db.ini";
    std::shared_ptr<TimescaleDB> timescaleDB;
    std::shared_ptr<RealTimeData> dataCollector;
    try {
        DBConfig config = loadConfig(configFilePath, logger);
        timescaleDB = std::make_shared<TimescaleDB>(
            logger, 
            config.dbname, 
            config.user, 
            config.password, 
            config.host, 
            config.port
        );
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Failed to initialize TimescaleDB: " + std::string(e.what()));
        return 1;
    }

    try {
        dataCollector = std::make_shared<RealTimeData>(logger, timescaleDB);
        STX_LOGI(logger, "Success to initialize RealTimeData.");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Failed to initialize RealTimeData: " + std::string(e.what()));
        return 1; // Exit the program if RealTimeData initialization fails
    }

    std::thread dataThread([&]() {
        try {
            dataCollector->start();
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Exception in data collection thread: " + std::string(e.what()));
            running = false;
        }
    });

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    STX_LOGI(logger, "Terminating the program gracefully...");

    dataCollector->stop(); 
    boost::interprocess::shared_memory_object::remove("RealTimeData");

    if (dataThread.joinable()) {
        dataThread.join();
    }

    STX_LOGI(logger, "Program terminated successfully.");

    return 0;
}