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

#include <numeric>
#include <cmath>
#include <thread>
#include <chrono>
#include "RealTimeData.h"

RealTimeData::RealTimeData(const std::shared_ptr<Logger>& log, const std::shared_ptr<TimescaleDB>& db)
    : osSignal(std::make_unique<EReaderOSSignal>(2000)), 
      client(std::make_unique<EClientSocket>(this, osSignal.get())), 
      reader(std::make_unique<EReader>(client.get(), osSignal.get())),
      logger(log), timescaleDB(db), nextOrderId(0), 
      requestId(0), yesterdayClose(0.0), running(false) {

    if (!logger) {
        throw std::runtime_error("Logger is null");
    }
    if (!timescaleDB) {
        STX_LOGE(logger, "TimescaleDB is null");
        throw std::runtime_error("TimescaleDB is null");
    }

    connected = false;
    previousVolume = 0

    try {
        connectToIB();
        STX_LOGI(logger, "RealTimeData object created successfully.");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error connecting to IB TWS: " + std::string(e.what()));
        stop();
    }
}

RealTimeData::~RealTimeData() {
    stop();
    if (readerThread.joinable()) {
        readerThread.join();
    }
    boost::interprocess::shared_memory_object::remove("RealTimeData");
}

bool RealTimeData::connectToIB() {
    std::lock_guard<std::mutex> lock(clientMutex);

    try {
        client = std::make_unique<EClientSocket>(this, osSignal.get());
        if (!client) {
            throw std::runtime_error("Failed to create EClientSocket");
        }

        const char *host = "127.0.0.1";
        int port = 7496;
        int clientId = 0;

        if (client->eConnect(host, port, clientId)) {
            if (!reader) {
                reader = std::make_unique<EReader>(client.get(), osSignal.get());
            }

            reader->start();
            readerThread = std::thread([this]() {
                while (client->isConnected()) {
                    osSignal->waitForSignal();
                    std::lock_guard<std::mutex> lock(clientMutex);
                    reader->processMsgs();
                }
            });

            connected = true;
            return true;
        } else {
            STX_LOGE(logger, "Failed to connect to IB TWS.");
            stop();
            return false;
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error during connectToIB: " + std::string(e.what()));
        stop();
        return false;
    }
}

void RealTimeData::start() {
    running = true;

    try {
        boost::interprocess::shared_memory_object::remove("RealTimeData");

        shm = boost::interprocess::shared_memory_object(boost::interprocess::create_only, "RealTimeData", boost::interprocess::read_write);
        shm.truncate(1024);
        region = boost::interprocess::mapped_region(shm, boost::interprocess::read_write);
        STX_LOGI(logger, "Shared memory RealTimeData created successfully.");

        requestData();
        
        std::thread processDataThread([&]() {
            while (running) {
                auto now = std::chrono::system_clock::now();
                auto nextMinute = std::chrono::time_point_cast<std::chrono::minutes>(now) + std::chrono::minutes(1);

                std::this_thread::sleep_until(nextMinute);

                aggregateMinuteData();
            }
        });

        processDataThread.join();
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Exception in start: " + std::string(e.what()));
    }

    stop();
}

void RealTimeData::stop() {
    if (!running) return;

    running = false;

    boost::interprocess::shared_memory_object::remove("RealTimeData");

    if (client && client->isConnected()) {
        client->eDisconnect();
        STX_LOGI(logger, "Disconnected from IB TWS");
    }

    {
        std::lock_guard<std::mutex> lock(clientMutex);
        client.reset();
        reader.reset();
    }
    
    STX_LOGI(logger, "RealTimeData stopped and cleaned up.");
}

void RealTimeData::requestData() {
    Contract contract;
    contract.symbol = "SPY";
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.primaryExchange = "NYSE";
    contract.currency = "USD";

    int l1RequestId = ++requestId;
    int l2RequestId = ++requestId;

    client->reqMktData(l1RequestId, contract, "", false, false, TagValueListSPtr());
    client->reqMktDepth(l2RequestId, contract, 10, true, TagValueListSPtr());
    
    STX_LOGI(logger, "Requested L1 data with request ID: " + std::to_string(l1RequestId));
    STX_LOGI(logger, "Requested L2 data with request ID: " + std::to_string(l2RequestId));
}

void RealTimeData::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib &attrib) {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (field == LAST) {
        l1Prices.push_back(price);
        STX_LOGI(logger, "Received tick price: Ticker " + std::to_string(tickerId) + ", Price " + std::to_string(price));
    }
}

void RealTimeData::tickSize(TickerId tickerId, TickType field, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (field == LAST_SIZE) {
        l1Volumes.push_back(size);
        STX_LOGI(logger, "Received tick size: Ticker " + std::to_string(tickerId) + ", Size " + std::to_string(size));
    }
}

void RealTimeData::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);

    processL2Data(position, price, size, side);

    STX_LOGI(logger, "Market Depth Update: Ticker " + std::to_string(id) +
                     ", Position " + std::to_string(position) +
                     ", Operation " + std::to_string(operation) +
                     ", Side " + std::to_string(side) +
                     ", Price " + std::to_string(price) +
                     ", Size " + std::to_string(size));
}

void RealTimeData::processL2Data(int position, double price, Decimal size, int side) {
    std::map<std::string, double> data;
    data["Position"] = position;

    if (side == 1) {
        data["BidPrice"] = price;
        data["BidSize"] = size;
    } else {
        data["AskPrice"] = price;
        data["AskSize"] = size;
    }

    l2Data.push_back(data);
}

void RealTimeData::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    STX_LOGE(logger, "Error: ID " + std::to_string(id) + " Code " + std::to_string(errorCode) + " Message: " + errorString);

    if (errorCode == 509 || errorCode == 1100 || errorCode == 1101) {
        STX_LOGW(logger, "Attempting to reconnect due to error code: " + std::to_string(errorCode));
        reconnect();
    }
}

void RealTimeData::reconnect() {
    while (true) {
        stop();
        if (connectToIB()) {
            STX_LOGI(logger, "Reconnected successfully.");
            break;
        } else {
            STX_LOGE(logger, "Reconnection attempt failed. Retrying in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

void RealTimeData::nextValidId(OrderId orderId) {
    nextOrderId = orderId;
    STX_LOGI(logger, "Next valid order ID: " + std::to_string(orderId));
}

void RealTimeData::aggregateMinuteData() {
    if (l1Prices.empty() || l2Data.empty()) {
        STX_LOGE(logger, "Empty data. ");
        return;
    }

    std::lock_guard<std::mutex> lock(dataMutex);

    double open = l1Prices.front();
    double close = l1Prices.back();
    double high = *std::max_element(l1Prices.begin(), l1Prices.end());
    double low = *std::min_element(l1Prices.begin(), l1Prices.end());
    double volume = l1Volumes.back() - previousVolume;
    previousVolume = l1Volumes.back();

    double bidAskSpread = l2Data.back().at("AskPrice") - l2Data.front().at("BidPrice");
    double midpoint = (l2Data.front().at("BidPrice") + l2Data.back().at("AskPrice")) / 2.0;

    double totalL2Volume = 0.0;
    for (const auto &data : l2Data) {
        totalL2Volume += data.at("BidSize") + data.at("AskSize");
    }

    double rsi = calculateRSI();
    double macd = calculateMACD();
    double vwap = calculateVWAP();

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    std::string datetime = oss.str();

    // Insert into TimescaleDB
    if (timescaleDB->insertL1Data(datetime, {{"Open", open}, {"High", high}, {"Low", low}, {"Close", close}, {"Volume", volume}})) {
        STX_LOGI(logger, "L1 data written to db successfully: " + datetime);
    } else {
        STX_LOGE(logger, "L1 data write to db failed: " + datetime);
    }
    if (timescaleDB->insertL2Data(datetime, l2Data)) {
        STX_LOGI(logger, "L2 data written to db successfully: " + datetime);
    } else {
        STX_LOGE(logger, "L2 data write to db failed: " + datetime);
    }

    // Write combined data to shared memory
    std::stringstream combinedData;
    combinedData << datetime << "," << open << "," << high << "," << low << "," << close << "," << volume << ","
                 << bidAskSpread << "," << midpoint << "," << totalL2Volume << "," << rsi << "," << macd << "," << vwap << "\n";
    writeToSharedMemory(combinedData.str());

    l1Prices.clear();
    l1Volumes.clear();
    l2Data.clear();
}

void RealTimeData::writeToSharedMemory(const std::string &data) {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::memset(region.get_address(), 0, region.get_size());
    std::memcpy(region.get_address(), data.c_str(), data.size());
    STX_LOGI(logger, "Data written to shared memory");
}

double RealTimeData::calculateRSI() {
    if (l1Prices.size() < 2) return 50.0;

    double gains = 0.0, losses = 0.0;
    for (size_t i = 1; i < l1Prices.size(); ++i) {
        double change = l1Prices[i] - l1Prices[i - 1];
        if (change > 0) {
            gains += change;
        } else {
            losses -= change;
        }
    }

    double rs = (losses == 0) ? gains : gains / losses;
    return 100.0 - (100.0 / (1.0 + rs));
}

double RealTimeData::calculateMACD() {
    if (l1Prices.size() < 26) return 0.0;

    double shortEMA = calculateEMA(12);
    double longEMA = calculateEMA(26);

    return shortEMA - longEMA;
}

double RealTimeData::calculateEMA(int period) {
    if (l1Prices.size() < period) return l1Prices.back();

    double multiplier = 2.0 / (period + 1);
    double ema = l1Prices[l1Prices.size() - period];

    for (size_t i = l1Prices.size() - period + 1; i < l1Prices.size(); ++i) {
        ema = (l1Prices[i] - ema) * multiplier + ema;
    }

    return ema;
}

double RealTimeData::calculateVWAP() {
    if (l1Prices.empty() || l1Volumes.empty()) return 0.0;

    double cumulativePriceVolume = 0.0;
    double cumulativeVolume = 0.0;

    for (size_t i = 0; i < l1Prices.size(); ++i) {
        cumulativePriceVolume += l1Prices[i] * l1Volumes[i];
        cumulativeVolume += l1Volumes[i];
    }

    return cumulativePriceVolume / cumulativeVolume;
}