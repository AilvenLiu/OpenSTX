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

#include "TimescaleDB.h"

TimescaleDB::TimescaleDB(const std::shared_ptr<Logger>& log, const std::string &dbname, const std::string &user, const std::string &password, const std::string &host, const std::string &port)
    : logger(log), conn(nullptr) {
    try {
        std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;
        conn = new pqxx::connection(connectionString);

        if (conn->is_open()) {
            STX_LOGI(logger, "Connected to TimescaleDB: " + dbname);
            createTables();
        } else {
            STX_LOGE(logger, "Failed to connect to TimescaleDB: " + dbname);
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error connecting to TimescaleDB: " + std::string(e.what()));
    }
}

TimescaleDB::~TimescaleDB() {
    if (conn) {
        delete conn;
        STX_LOGI(logger, "Disconnected from TimescaleDB");
    }
}

void TimescaleDB::createTables() {
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
                total_l2_volume DOUBLE PRECISION
            );
        )");

        txn.commit();
        STX_LOGI(logger, "Tables created or verified successfully");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error creating tables in TimescaleDB: " + std::string(e.what()));
    }
}

void TimescaleDB::insertL1Data(const std::string &datetime, const std::map<std::string, double> &l1Data) {
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
    try {
        pqxx::work txn(*conn);

        std::string query = "INSERT INTO feature_data (datetime, gap, today_open, total_l2_volume) VALUES (" +
                            txn.quote(datetime) + ", " +
                            txn.quote(features.at("Gap")) + ", " +
                            txn.quote(features.at("TodayOpen")) + ", " +
                            txn.quote(features.at("TotalL2Volume")) + ");";

        txn.exec(query);
        txn.commit();

        STX_LOGI(logger, "Inserted feature data at " + datetime);
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error inserting feature data into TimescaleDB: " + std::string(e.what()));
    }
}