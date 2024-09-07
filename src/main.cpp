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

bool isMarketOpenTime(const std::shared_ptr<Logger>& logger) {
    auto now = std::chrono::system_clock::now();
    auto utc_time = std::chrono::system_clock::to_time_t(now);
    
    std::tm ny_time{};
    setenv("TZ", "America/New_York", 1);
    tzset();
    localtime_r(&utc_time, &ny_time);

    bool open = (ny_time.tm_hour >= 9 && ny_time.tm_hour < 16);
    bool weekend = (ny_time.tm_wday == 0 || ny_time.tm_wday == 6);
    
    std::ostringstream oss;
    oss << std::put_time(&ny_time, "%Y-%m-%d %H:%M:%S");
    std::string datetime = oss.str();

    bool opened = (open && weekend);
    STX_LOGD(logger, "Current New York Time: " + datetime + ", week: " + std::to_string(ny_time.tm_wday) + ", market is " + (opened ? " open" : " close"));

    return open && !weekend;
}

int main(int argc, char* argv[]) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);

    LogLevel logLevel = INFO;  // Default log level

    if (argc >= 2) {
        std::string logLevelStr = argv[1];
        try {
            logLevel = Logger::stringToLogLevel(logLevelStr);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid log level: " << logLevelStr << std::endl;
            return 1;
        }
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

    std::string logFilePath = "logs/OpenSTX_" + timestamp + ".log";
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

        dataCollector = std::make_shared<RealTimeData>(logger, timescaleDB);
        STX_LOGI(logger, "Successfully initialized RealTimeData.");

        historicalDataFetcher = std::make_shared<DailyDataFetcher>(logger, timescaleDB);
        STX_LOGI(logger, "Successfully initialized DailyDataFetcher.");
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Initialization failed: " + std::string(e.what()));
        return 1;
    }

    std::thread realTimeDataThread([&]() {
        while (running.load()) {
            try {
                // Wait for market to open
                while (!isMarketOpenTime(logger) && running.load()) {
                    std::unique_lock<std::mutex> lock(cvMutex);
                    cv.wait_for(lock, std::chrono::minutes(1), [] { return !running.load(); });
                }

                if (!running.load()) continue;

                STX_LOGI(logger, "Market opening, starting RealTimeData collection.");
                
                if (!dataCollector->start()) {
                    STX_LOGE(logger, "Failed to start RealTimeData collection.");
                    continue; // Skip the rest of the loop and retry
                }

                STX_LOGI(logger, "RealTimeData collection active during market hours.");
                
                // Collect data while market is open
                while (isMarketOpenTime(logger) && running.load()) {
                    std::unique_lock<std::mutex> lock(cvMutex);
                    cv.wait_for(lock, std::chrono::seconds(10), [] { return !running.load(); });
                }

                STX_LOGI(logger, "Market closed, stopping RealTimeData collection.");
                dataCollector->stop();

                // Wait a bit before checking market status again
                std::unique_lock<std::mutex> lock(cvMutex);
                cv.wait_for(lock, std::chrono::minutes(1), [] { return !running.load(); });

            } catch (const std::exception &e) {
                STX_LOGE(logger, "Exception caught in realTimeDataThread: " + std::string(e.what()));
                std::unique_lock<std::mutex> lock(cvMutex);
                cv.wait_for(lock, std::chrono::minutes(1), [] { return !running.load(); });
            }
        }
    });

    std::thread historicalDataThread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        while (running.load()) {
            try {
                if (!isMarketOpenTime(logger)) {
                    historicalDataFetcher->fetchAndProcessDailyData("ALL", "10 Y", true);
                    STX_LOGI(logger, "Historical data fetch complete, sleeping for an hour.");
                    for (int i = 0; i < 60 && running.load(); ++i) {
                        std::unique_lock<std::mutex> lock(cvMutex);
                        cv.wait_for(lock, std::chrono::minutes(1), [] { return !running.load(); });
                    }
                } else {
                    STX_LOGI(logger, "Market is open. Historical data fetch paused.");
                    // Sleep until market closes
                    while (isMarketOpenTime(logger) && running.load()) {
                        std::unique_lock<std::mutex> lock(cvMutex);
                        cv.wait_for(lock, std::chrono::minutes(1), [] { return !running.load(); });
                    }
                }
            } catch (const std::exception &e) {
                STX_LOGE(logger, "Exception caught in historicalDataThread: " + std::string(e.what()));
                std::unique_lock<std::mutex> lock(cvMutex);
                cv.wait_for(lock, std::chrono::minutes(5), [] { return !running.load(); });
            }
        }
    });

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    STX_LOGI(logger, "Terminating the program gracefully...");

    dataCollector->stop(); 
    historicalDataFetcher->stop();

    if (realTimeDataThread.joinable()) realTimeDataThread.join();
    if (historicalDataThread.joinable()) historicalDataThread.join();

    STX_LOGI(logger, "Program terminated successfully.");

    return 0;
}