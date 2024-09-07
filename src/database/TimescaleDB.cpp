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
#include "nlohmann/json.hpp"
#include "TimescaleDB.hpp"

using json = nlohmann::json;

TimescaleDB::TimescaleDB(const std::shared_ptr<Logger>& log, const std::string &_dbname, const std::string &_user, const std::string &_password, const std::string &_host, const std::string &_port)
    : logger(log), conn(nullptr), dbname(_dbname), user(_user), password(_password), host(_host), port(_port), running(true) {
    try {
        connectToDatabase();
        monitoringThread = std::thread(&TimescaleDB::checkAndReconnect, this);
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error initializing TimescaleDB: " + std::string(e.what()));
        cleanupAndExit();
    }
}

TimescaleDB::~TimescaleDB() {
    STX_LOGI(logger, "Destructor called, cleaning up resources.");
    running.store(false);
    if (monitoringThread.joinable()) {
        monitoringThread.join();
    }
    if (conn) {
        conn.reset();
        STX_LOGI(logger, "Disconnected from TimescaleDB.");
    }
}

void TimescaleDB::connectToDatabase() {
    std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;
    conn = std::make_unique<pqxx::connection>(connectionString);

    if (conn->is_open()) {
        STX_LOGI(logger, "Connected to TimescaleDB: " + dbname);
        enableTimescaleExtension();
        createTables();
    } else {
        STX_LOGE(logger, "Failed to connect to TimescaleDB: " + dbname);
        cleanupAndExit();
    }
}

void TimescaleDB::createDatabase(const std::string &dbname, const std::string &user, const std::string &password, const std::string &host, const std::string &port) {
    try {
        STX_LOGI(logger, "Attempting to create database: " + dbname);

        std::string adminConnectionString = "dbname=postgres user=" + user + " password=" + password + " host=" + host + " port=" + port;
        pqxx::connection adminConn(adminConnectionString);

        if (adminConn.is_open()) {
            pqxx::nontransaction txn(adminConn);

            txn.exec("CREATE DATABASE " + dbname + " TABLESPACE openstx_space;");
            STX_LOGI(logger, "Database created successfully in tablespace openstx_space.");

            connectToDatabase();
        } else {
            STX_LOGE(logger, "Failed to connect to the PostgreSQL server to create the database.");
            cleanupAndExit();
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error creating TimescaleDB database: " + std::string(e.what()));
        cleanupAndExit();
    }
}

void TimescaleDB::enableTimescaleExtension() {
    STX_LOGI(logger, "Attempting to enable TimescaleDB extension.");
    try {
        pqxx::work txn(*conn);
        txn.exec("CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;");
        txn.commit();
        STX_LOGI(logger, "TimescaleDB extension enabled.");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error enabling TimescaleDB extension: " + std::string(e.what()));
        reconnect(5, 2);
    }
}

void TimescaleDB::reconnect(int max_attempts, int delay_seconds) {
    int attempts = 0;
    while (attempts < max_attempts) {
        STX_LOGI(logger, "Attempting to reconnect to TimescaleDB. Attempt " + std::to_string(attempts + 1) + " of " + std::to_string(max_attempts));
        try {
            conn.reset();
            connectToDatabase();
            return;
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Error reconnecting to TimescaleDB: " + std::string(e.what()));
        }
        attempts++;
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }
    STX_LOGE(logger, "Failed to reconnect to TimescaleDB after " + std::to_string(max_attempts) + " attempts.");
    cleanupAndExit();
}

void TimescaleDB::checkAndReconnect() {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Check every 30 seconds

        if (!conn || !conn->is_open()) {
            STX_LOGW(logger, "Database connection lost. Attempting to reconnect...");
            reconnect(5, 2); // Attempt to reconnect with 5 attempts and 2 seconds delay
        }
    }
}

void TimescaleDB::createTables() {
    STX_LOGI(logger, "Attempting to create or verify tables.");
    try {
        pqxx::work txn(*conn);

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS realtime_data (
                datetime TIMESTAMPTZ PRIMARY KEY,
                l1_data JSONB,
                l2_data JSONB,
                feature_data JSONB
            );
        )");

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS daily_data (
                date DATE,
                symbol TEXT,
                open DOUBLE PRECISION,
                high DOUBLE PRECISION,
                low DOUBLE PRECISION,
                close DOUBLE PRECISION,
                volume DOUBLE PRECISION,
                adj_close DOUBLE PRECISION,
                sma DOUBLE PRECISION,
                ema DOUBLE PRECISION,
                rsi DOUBLE PRECISION,
                macd DOUBLE PRECISION,
                vwap DOUBLE PRECISION,
                momentum DOUBLE PRECISION,
                PRIMARY KEY (date, symbol)
            );
        )");

        txn.commit();
        STX_LOGI(logger, "Tables created or verified successfully.");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error creating tables in TimescaleDB: " + std::string(e.what()));
    }
}

void TimescaleDB::cleanupAndExit() {
    STX_LOGI(logger, "Cleaning up resources before exit...");
    if (conn) {
        conn.reset();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    STX_LOGI(logger, "Resources cleaned up. Exiting program due to error.");
    exit(EXIT_FAILURE);
}

bool TimescaleDB::insertRealTimeData(const std::string &datetime, const json &l1Data, const json &l2Data, const json &featureData) {
    STX_LOGI(logger, "Inserting real-time data at " + datetime);
    try {
        pqxx::work txn(*conn);

        std::string query = "INSERT INTO realtime_data (datetime, l1_data, l2_data, feature_data) VALUES (" +
                            txn.quote(datetime) + ", " +
                            txn.quote(l1Data.dump()) + ", " +
                            txn.quote(l2Data.dump()) + ", " +
                            txn.quote(featureData.dump()) + ");";

        txn.exec(query);
        txn.commit();

        STX_LOGI(logger, "Inserted real-time data at " + datetime);
        return true;
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error inserting real-time data into TimescaleDB: " + std::string(e.what()));
        return false;
    }
}

bool TimescaleDB::insertOrUpdateDailyData(const std::string &date, const std::map<std::string, std::variant<double, std::string>> &dailyData) {
    STX_LOGI(logger, "Inserting or updating daily data for date " + date);
    try {
        pqxx::work txn(*conn);

        std::string query = "INSERT INTO daily_data (date, symbol, open, high, low, close, volume, adj_close, sma, ema, rsi, macd, vwap, momentum) VALUES (" +
                            txn.quote(date) + ", " +
                            txn.quote(std::get<std::string>(dailyData.at("symbol"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("open"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("high"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("low"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("close"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("volume"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("adj_close"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("sma"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("ema"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("rsi"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("macd"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("vwap"))) + ", " +
                            txn.quote(std::get<double>(dailyData.at("momentum"))) + ") " +
                            "ON CONFLICT (date, symbol) DO UPDATE SET " +
                            "open = EXCLUDED.open, high = EXCLUDED.high, low = EXCLUDED.low, close = EXCLUDED.close, " +
                            "volume = EXCLUDED.volume, adj_close = EXCLUDED.adj_close, sma = EXCLUDED.sma, " +
                            "ema = EXCLUDED.ema, rsi = EXCLUDED.rsi, macd = EXCLUDED.macd, vwap = EXCLUDED.vwap, " +
                            "momentum = EXCLUDED.momentum;";

        txn.exec(query);
        txn.commit();
        STX_LOGI(logger, "Inserted or updated daily data for date " + date);
        return true;
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error inserting or updating daily data into TimescaleDB: " + std::string(e.what()));
        return false;
    }
}

const std::string TimescaleDB::getLastDailyEndDate(const std::string &symbol) {
    try {
        pqxx::work txn(*conn);
        std::string query = "SELECT MAX(date) FROM daily_data WHERE symbol = " + txn.quote(symbol) + ";";
        pqxx::result result = txn.exec(query);

        if (!result.empty() && !result[0][0].is_null()) {
            std::string lastDate = result[0][0].as<std::string>();
            STX_LOGI(logger, "Last daily end date for " + symbol + ": " + lastDate);
            return lastDate; // Return the date as is
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error retrieving the last daily end date: " + std::string(e.what()));
    }

    return ""; // Return empty string if no data is found or error occurs
}

const std::string TimescaleDB::getFirstDailyStartDate(const std::string &symbol) {
    try {
        pqxx::work txn(*conn);
        std::string query = "SELECT MIN(date) FROM daily_data WHERE symbol = " + txn.quote(symbol) + ";";
        pqxx::result result = txn.exec(query);

        if (!result.empty() && !result[0][0].is_null()) {
            std::string firstDate = result[0][0].as<std::string>();
            STX_LOGI(logger, "First daily start date for " + symbol + ": " + firstDate);
            return firstDate;
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error retrieving the first daily start date: " + std::string(e.what()));
    }

    return ""; // Return empty string if no data is found or error occurs
}

