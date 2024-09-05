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

#include <thread>
#include <chrono>
#include <signal.h>
#include <iostream>
#include <filesystem>
#include <memory>
#include <atomic>
#include <csignal>
#include <condition_variable>

#include "RealTimeData.hpp"
#include "DailyDataFetcher.hpp"
#include "TimescaleDB.hpp"
#include "Logger.hpp"
#include "Config.hpp"

std::atomic<bool> running(true);
std::condition_variable cv;
std::mutex cvMutex;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    running.store(false);
    cv.notify_all();
}

enum ModeType {
    kTypeModeRealtime,
    kTypeModeDaily,
    kTypeModeBoth,
    kTypeModeInvalid // For invalid mode
};

ModeType ModeStrToType(const std::string& mode) {
    if (mode == "realtime") {
        return kTypeModeRealtime;
    } else if (mode == "daily") {
        return kTypeModeDaily;
    } else if (mode == "both") {
        return kTypeModeBoth;
    } else {
        return kTypeModeInvalid;
    }
}

int main(int argc, char* argv[]) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);

    LogLevel logLevel = DEBUG; // Set default log level to DEBUG

    bool runRealTime = false;
    bool runDaily = false; 
    if (argc >= 2) {
        std::string mode = argv[1];
        switch (ModeStrToType(mode)) {
            case kTypeModeRealtime: 
                runRealTime = true;
                break;
            case kTypeModeDaily:
                runDaily = true;
                break;
            case kTypeModeBoth:
                runRealTime = true;
                runDaily = true;
                break;
            default:
                std::cerr << "mode is allowed only: [realtime], [daily], [both]";
                return 1;
        }
    } else {
        runRealTime = true;
        std::cout << "no arguments are given, use realtime as default.";
    }


    std::string logDir = "logs";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d-%H:%M:%S");
    std::string timestamp = oss.str();

    std::string logFilePath = "logs/Test_OpenSTX_" + timestamp + ".log";
    auto logger = std::make_shared<Logger>(logFilePath, logLevel);
    STX_LOGI(logger, "Start main");

    std::shared_ptr<TimescaleDB> timescaleDB;
    std::shared_ptr<RealTimeData> dataCollector;
    std::shared_ptr<DailyDataFetcher> historicalDataFetcher;

    try {
        std::string configFilePath = "conf/alicloud_db.ini";
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
        STX_LOGE(logger, "Initialization failed: " + std::string(e.what()));
        return 1;
    }

    if (runRealTime) {
        auto realTimeData = std::make_shared<RealTimeData>(logger, timescaleDB); // Pass nullptr for db
        if (!realTimeData->start()) {
            std::cerr << "Failed to start real-time data collection." << std::endl;
            return 1;
        }

        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        realTimeData->stop();
    }

    if (runDaily){
        // Run daily data collection
        auto dailyDataFetcher = std::make_shared<DailyDataFetcher>(logger, timescaleDB); // Pass nullptr for db
        dailyDataFetcher->fetchAndProcessDailyData("SPY", "1 M", true);

        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        dailyDataFetcher->stop();
    }

    STX_LOGI(logger, "Program terminated successfully.");

    return 0;
}