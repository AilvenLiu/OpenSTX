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

#include "TimescaleDB.h"

TimescaleDB::TimescaleDB(const std::shared_ptr<Logger>& log, const std::string &_dbname, const std::string &_user, const std::string &_password, const std::string &_host, const std::string &_port) 
    : logger(log), conn(nullptr), dbname(_dbname), user(_user), password(_password), host(_host), port(_port) {
    try {
        std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;

        try {
            conn = new pqxx::connection(connectionString);
            if (conn->is_open()) {
                STX_LOGI(logger, "Connected to TimescaleDB: " + dbname);
                enableTimescaleExtension();
                createTables();
            } else {
                STX_LOGE(logger, "Failed to connect to TimescaleDB: " + dbname);
                cleanupAndExit();
            }
        } catch (const pqxx::broken_connection& e) {
            STX_LOGW(logger, "Database does not exist. Attempting to create database: " + dbname);
            createDatabase(dbname, user, password, host, port);
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error initializing TimescaleDB: " + std::string(e.what()));
        cleanupAndExit();
    }
}

TimescaleDB::~TimescaleDB() {
    STX_LOGI(logger, "Destructor called, cleaning up resources.");
    if (conn) {
        delete conn;
        conn = nullptr;
        STX_LOGI(logger, "Disconnected from TimescaleDB.");
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

            std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;
            conn = new pqxx::connection(connectionString);
            if (conn->is_open()) {
                STX_LOGI(logger, "Connected to TimescaleDB: " + dbname);

                // 在重新连接后立即检查扩展状态并重新初始化对象
                enableTimescaleExtension();

                createTables();
            } else {
                STX_LOGE(logger, "Failed to connect to TimescaleDB after creation: " + dbname);
                cleanupAndExit();
            }
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
        reconnect(5, 2);  // 尝试最多重连5次，每次重连之间等待2秒
    }
}

void TimescaleDB::reconnect(int max_attempts, int delay_seconds) {
    int attempts = 0;
    while (attempts < max_attempts) {
        STX_LOGI(logger, "Attempting to reconnect to TimescaleDB. Attempt " + std::to_string(attempts + 1) + " of " + std::to_string(max_attempts));
        try {
            if (conn) {
                delete conn;
                conn = nullptr;
            }

            std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;
            conn = new pqxx::connection(connectionString);

            if (conn->is_open()) {
                STX_LOGI(logger, "Reconnected to TimescaleDB: " + dbname);

                // 再次尝试启用 TimescaleDB 扩展
                try {
                    pqxx::work txn(*conn);
                    txn.exec("CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;");
                    txn.commit();
                    STX_LOGI(logger, "TimescaleDB extension enabled.");
                    return;  // 成功后退出循环
                } catch (const std::exception &e) {
                    STX_LOGE(logger, "Error enabling TimescaleDB extension after reconnect: " + std::string(e.what()));
                }
            } else {
                STX_LOGE(logger, "Failed to reconnect to TimescaleDB: " + dbname);
            }
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Error reconnecting to TimescaleDB: " + std::string(e.what()));
        }

        attempts++;
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));  // 增加延迟时间
    }

    STX_LOGE(logger, "Failed to reconnect to TimescaleDB after " + std::to_string(max_attempts) + " attempts.");
    cleanupAndExit();
}

void TimescaleDB::createTables() {
    STX_LOGI(logger, "Attempting to create or verify tables.");
    try {
        pqxx::work txn(*conn);

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS l1_data (
                datetime TIMESTAMPTZ PRIMARY KEY,
                bid DOUBLE PRECISION,
                ask DOUBLE PRECISION,
                last DOUBLE PRECISION,
                open DOUBLE PRECISION,
                high DOUBLE PRECISION,
                low DOUBLE PRECISION,
                close DOUBLE PRECISION,
                volume DOUBLE PRECISION
            );
        )");

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS l2_data (
                datetime TIMESTAMPTZ,
                price_level INT,
                bid_price DOUBLE PRECISION,
                bid_size DOUBLE PRECISION,
                ask_price DOUBLE PRECISION,
                ask_size DOUBLE PRECISION,
                PRIMARY KEY (datetime, price_level)
            );
        )");

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS feature_data (
                datetime TIMESTAMPTZ PRIMARY KEY,
                gap DOUBLE PRECISION,
                today_open DOUBLE PRECISION,
                total_l2_volume DOUBLE PRECISION,
                rsi DOUBLE PRECISION,
                macd DOUBLE PRECISION,
                vwap DOUBLE PRECISION
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
        delete conn;
        conn = nullptr;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1)); // 保证日志记录的稳定性

    STX_LOGI(logger, "Resources cleaned up. Exiting program due to error.");
    exit(EXIT_FAILURE);  // Graceful exit
}

void TimescaleDB::insertL1Data(const std::string &datetime, const std::map<std::string, double> &l1Data) {
    STX_LOGI(logger, "Inserting L1 data at " + datetime);
    try {
        pqxx::work txn(*conn);

        std::string query = "INSERT INTO l1_data (datetime, bid, ask, last, open, high, low, close, volume) VALUES (" +
                            txn.quote(datetime) + ", " +
                            txn.quote(l1Data.at("Bid")) + ", " +
                            txn.quote(l1Data.at("Ask")) + ", " +
                            txn.quote(l1Data.at("Last")) + ", " +
                            txn.quote(l1Data.at("Open")) + ", " +
                            txn.quote(l1Data.at("High")) + ", " +
                            txn.quote(l1Data.at("Low")) + ", " +
                            txn.quote(l1Data.at("Close")) + ", " +
                            txn.quote(l1Data.at("Volume")) + ");";

        txn.exec(query);
        txn.commit();

        STX_LOGI(logger, "Inserted L1 data at " + datetime);
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error inserting L1 data into TimescaleDB: " + std::string(e.what()));
    }
}

void TimescaleDB::insertL2Data(const std::string &datetime, const std::vector<std::map<std::string, double>> &l2Data) {
    STX_LOGI(logger, "Inserting L2 data at " + datetime);
    try {
        pqxx::work txn(*conn);

        for (size_t i = 0; i < l2Data.size(); ++i) {
            const auto &data = l2Data[i];

            std::string query = "INSERT INTO l2_data (datetime, price_level, bid_price, bid_size, ask_price, ask_size) VALUES (" +
                                txn.quote(datetime) + ", " +
                                txn.quote(static_cast<int>(i)) + ", " +
                                txn.quote(data.at("BidPrice")) + ", " +
                                txn.quote(data.at("BidSize")) + ", " +
                                txn.quote(data.at("AskPrice")) + ", " +
                                txn.quote(data.at("AskSize")) + ");";

            txn.exec(query);
        }

        txn.commit();

        STX_LOGI(logger, "Inserted L2 data at " + datetime);
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error inserting L2 data into TimescaleDB: " + std::string(e.what()));
    }
}

void TimescaleDB::insertFeatureData(const std::string &datetime, const std::map<std::string, double> &features) {
    STX_LOGI(logger, "Inserting feature data at " + datetime);
    try {
        pqxx::work txn(*conn);

        std::string query = "INSERT INTO feature_data (datetime, gap, today_open, total_l2_volume, rsi, macd, vwap) VALUES (" +
                            txn.quote(datetime) + ", " +
                            txn.quote(features.at("Gap")) + ", " +
                            txn.quote(features.at("TodayOpen")) + ", " +
                            txn.quote(features.at("TotalL2Volume")) + ", " +
                            txn.quote(features.at("RSI")) + ", " +
                            txn.quote(features.at("MACD")) + ", " +
                            txn.quote(features.at("VWAP")) + ");";

        txn.exec(query);
        txn.commit();

        STX_LOGI(logger, "Inserted feature data at " + datetime);
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error inserting feature data into TimescaleDB: " + std::string(e.what()));
    }
}