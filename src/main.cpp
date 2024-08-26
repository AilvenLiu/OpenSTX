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

#include "RealTimeData.h"
#include "DailyDataFetcher.h"
#include "TimescaleDB.h"
#include "Logger.h"
#include "Config.h"

std::atomic<bool> running(true);

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    running = false;
}

bool isMarketOpenTime() {
    std::time_t t = std::time(nullptr);
    std::tm *utc_tm = std::gmtime(&t);

    utc_tm->tm_hour -= 5;
    std::time_t localTime = std::mktime(utc_tm);

    std::tm *ny_tm = std::localtime(&localTime);
    if (ny_tm->tm_isdst > 0) {
        ny_tm->tm_hour += 1;
    }

    // Market open from 9:30 to 16:00 New York time
    bool open = (ny_tm->tm_hour > 9 && ny_tm->tm_hour < 16) || (ny_tm->tm_hour == 9 && ny_tm->tm_min >= 30);

    // Check if it's a weekend
    bool weekend = (ny_tm->tm_wday == 0 || ny_tm->tm_wday == 6);

    return open && !weekend;
}

int main() {
    signal(SIGINT, signalHandler);

    std::string logDir = "logs";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
    }

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d-%H:%M:%S");
    std::string timestamp = oss.str();

    std::string logFilePath = "logs/OpenSTX_" + timestamp + ".log";
    std::shared_ptr<Logger> logger = std::make_shared<Logger>(logFilePath);
    STX_LOGI(logger, "Start main");

    // Initialize TimescaleDB
    std::string configFilePath = "conf/alicloud_db.ini";
    std::shared_ptr<TimescaleDB> timescaleDB;
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

    // Initialize RealTimeData
    std::shared_ptr<RealTimeData> dataCollector;
    try {
        dataCollector = std::make_shared<RealTimeData>(logger, timescaleDB);
        STX_LOGI(logger, "Successfully initialized RealTimeData.");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Failed to initialize RealTimeData: " + std::string(e.what()));
        return 1;
    }

    // Initialize DailyDataFetcher
    std::shared_ptr<DailyDataFetcher> historicalDataFetcher;
    try {
        historicalDataFetcher = std::make_shared<DailyDataFetcher>(logger, timescaleDB);
        STX_LOGI(logger, "Successfully initialized DailyDataFetcher.");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Failed to initialize DailyDataFetcher: " + std::string(e.what()));
        return 1;
    }

    // RealTimeData management thread
    std::thread realTimeDataThread([&]() {
        while (running) {
            try {
                // Start data collection shortly before market opens
                while (!isMarketOpenTime()) {
                    std::this_thread::sleep_for(std::chrono::minutes(1));
                }
                STX_LOGI(logger, "Market opening soon, starting RealTimeData collection.");
                dataCollector->start();

                // Stop data collection shortly after market closes
                while (isMarketOpenTime()) {
                    std::this_thread::sleep_for(std::chrono::minutes(1));
                }
                STX_LOGI(logger, "Market closed, stopping RealTimeData collection.");
                dataCollector->stop();
            } catch (const std::exception &e) {
                STX_LOGE(logger, "Exception in RealTimeData management thread: " + std::string(e.what()));
                running = false;
            }
        }
    });

    // Historical data fetching thread
    std::thread historicalDataThread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(30)); // Wait for initial RealTimeData connection to stabilize
        while (running) {
            try {
                if (!isMarketOpenTime()) {
                    historicalDataFetcher->fetchAndProcessDailyData("SPY", "3 Y", true);
                    STX_LOGI(logger, "Historical data fetch complete, sleeping for an hour.");
                    std::this_thread::sleep_for(std::chrono::hours(1));
                } else {
                    STX_LOGI(logger, "Market is open. Historical data fetch paused.");
                    std::this_thread::sleep_for(std::chrono::hours(1));
                }
            } catch (const std::exception &e) {
                STX_LOGE(logger, "Exception in DailyDataFetcher thread: " + std::string(e.what()));
            }
        }
    });

    // Main loop waiting for termination signal
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    STX_LOGI(logger, "Terminating the program gracefully...");

    dataCollector->stop(); 
    historicalDataFetcher->stop();
    boost::interprocess::shared_memory_object::remove("RealTimeData");

    if (realTimeDataThread.joinable()) realTimeDataThread.join();
    if (historicalDataThread.joinable()) historicalDataThread.join();

    STX_LOGI(logger, "Program terminated successfully.");

    return 0;
}