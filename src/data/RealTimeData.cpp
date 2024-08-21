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
#include "RealTimeData.h"

RealTimeData::RealTimeData(const std::shared_ptr<Logger>& log, const std::shared_ptr<TimescaleDB>& db)
    : osSignal(std::make_unique<EReaderOSSignal>(2000)), 
      client(std::make_unique<EClientSocket>(this, osSignal.get())), 
      logger(log), timescaleDB(db), nextOrderId(0), 
      requestId(0), yesterdayClose(0.0), running(false) {

    if (!logger) {
        std::cerr << "Logger is null" << std::endl;
        throw std::runtime_error("Logger is null");
    }
    if (!timescaleDB) {
        STX_LOGE(logger, "TimescaleDB is null");
        throw std::runtime_error("TimescaleDB is null");
    }

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

void RealTimeData::connectToIB() {
    std::lock_guard<std::mutex> lock(clientMutex);

    STX_LOGI(logger, "Creating EClientSocket.");

    try {
        if (!client) {
            throw std::runtime_error("Failed to create EClientSocket");
        }
        STX_LOGI(logger, "EClientSocket created, attempting to connect.");
        
        const char *host = "127.0.0.1";
        int port = 7496;
        int clientId = 0;

        if (client->eConnect(host, port, clientId)) {
            STX_LOGI(logger, "Connected to IB TWS");

            reader = std::make_unique<EReader>(client.get(), osSignal.get());
            reader->start();

            readerThread = std::thread([this]() {
                while (client->isConnected()) {
                    osSignal->waitForSignal();
                    std::lock_guard<std::mutex> lock(clientMutex);
                    reader->processMsgs();
                }
            });
        } else {
            STX_LOGE(logger, "Failed to connect to IB TWS.");
            stop();
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error during connectToIB: " + std::string(e.what()));
        stop();
    } catch (...) {
        STX_LOGE(logger, "Unknown error occurred during connectToIB");
        stop();
    }
}

void RealTimeData::start() {
    running = true;

    try {
        boost::interprocess::shared_memory_object::remove("RealTimeData");

        shm = boost::interprocess::shared_memory_object(boost::interprocess::create_only, "RealTimeData", boost::interprocess::read_write);
        shm.truncate(1024);  // 根据需要调整大小
        region = boost::interprocess::mapped_region(shm, boost::interprocess::read_write);
        STX_LOGI(logger, "Shared memory RealTimeData created successfully.");

        std::time_t lastMinute = std::time(nullptr) / 60;

        // 数据请求线程
        std::thread requestDataThread([&]() {
            while (running) {
                if (isMarketOpen()) {
                    requestDataWithRetry();
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } else {
                    std::this_thread::sleep_for(std::chrono::minutes(1));
                }
            }
        });

        // 数据处理线程
        std::thread processDataThread([&]() {
            while (running) {
                std::time_t currentMinute = std::time(nullptr) / 60;
                if (currentMinute != lastMinute) {
                    aggregateMinuteData();
                    lastMinute = currentMinute;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });

        requestDataThread.join();
        processDataThread.join();
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Exception in start: " + std::string(e.what()));
    }

    stop();
}

void RealTimeData::stop() {
    if (!running) return; // 避免重复调用

    running = false;

    // 清理共享内存
    boost::interprocess::shared_memory_object::remove("RealTimeData");

    // 确保客户端正确断开连接
    if (client && client->isConnected()) {
        client->eDisconnect();
        STX_LOGI(logger, "Disconnected from IB TWS");
    }

    {
        std::lock_guard<std::mutex> lock(clientMutex);
        client.reset();
        reader.reset();  // 清理 reader
    }
    
    STX_LOGI(logger, "RealTimeData stopped and cleaned up.");
}

void RealTimeData::requestDataWithRetry() {
    if (!client->isConnected()) {
        STX_LOGW(logger, "Client is not connected. Attempting to reconnect...");
        try {
            connectToIB();
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Failed to reconnect: " + std::string(e.what()));
            return;
        }
    }
    
    try {
        requestData();
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Data request failed after reconnect: " + std::string(e.what()));
    }
}

bool RealTimeData::isMarketOpen() {
    std::time_t nyTime = getNYTime();
    std::tm *tm = std::localtime(&nyTime);

    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);

    // Market open from 9:30 to 16:00
    bool open = (tm->tm_hour > 9 && tm->tm_hour < 16) || (tm->tm_hour == 9 && tm->tm_min >= 30);

    // Check if it's a weekend
    bool weekend = (tm->tm_wday == 0 || tm->tm_wday == 6); // Sunday == 0, Saturday == 6

    if (open && !weekend) {
        STX_LOGI(logger, "Current New York Time: " + std::string(buf) + " - Market is open.");
        return true;
    } else {
        STX_LOGW(logger, "Current New York Time: " + std::string(buf) + " - Market is closed.");
        return false;
    }
}

std::time_t RealTimeData::getNYTime() {
    std::time_t t = std::time(nullptr);
    std::tm *utc_tm = std::gmtime(&t);

    // 考虑夏令时，手动计算 EST/EDT 时间
    utc_tm->tm_hour -= 5;  // 默认设置为 EST (UTC-5)
    std::time_t localTime = std::mktime(utc_tm);

    std::tm *ny_tm = std::localtime(&localTime);
    if (ny_tm->tm_isdst > 0) {
        ny_tm->tm_hour += 1;  // 如果是夏令时，将时间调整为 EDT (UTC-4)
    }

    return std::mktime(ny_tm);
}

void RealTimeData::requestData() {
    Contract contract;
    contract.symbol = "SPY";
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";

    // Request L1 and L2 data
    client->reqMktData(++requestId, contract, "", false, false, TagValueListSPtr());
    client->reqMktDepth(++requestId, contract, 10, true, TagValueListSPtr()); // Request 10 levels of market depth
}

void RealTimeData::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib &attrib) {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (field == LAST) {
        l1Prices.push_back(price);
    }

    // Write to shared memory
    std::ostringstream oss;
    oss << "Tick Price: " << tickerId << " Field: " << field << " Price: " << price;
    STX_LOGI(logger, oss.str());
}

void RealTimeData::tickSize(TickerId tickerId, TickType field, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (field == LAST_SIZE) {
        l1Volumes.push_back(size);
    }

    // Write to shared memory
    std::ostringstream oss;
    oss << "Tick Size: " << tickerId << " Field: " << field << " Size: " << size;
    STX_LOGI(logger, oss.str());
}

void RealTimeData::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);
    processL2Data(position, price, size, side);

    std::ostringstream oss;
    oss << "Update Mkt Depth: " << id << " Position: " << position << " Operation: " << operation << " Side: " << side << " Price: " << price << " Size: " << size;
    STX_LOGI(logger, oss.str());
}

void RealTimeData::processL2Data(int position, double price, Decimal size, int side) {
    std::map<std::string, double> data;
    data["Position"] = position;

    if (side == 1) {  // Bid side
        data["BidPrice"] = price;
        data["BidSize"] = size;
        data["AskPrice"] = 0.0; // Not available
        data["AskSize"] = 0.0; // Not available
    } else {  // Ask side
        data["BidPrice"] = 0.0; // Not available
        data["BidSize"] = 0.0; // Not available
        data["AskPrice"] = price;
        data["AskSize"] = size;
    }

    l2Data.push_back(data);
}

void RealTimeData::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    std::lock_guard<std::mutex> lock(dataMutex);

    std::ostringstream oss;
    oss << "Error ID: " << id << " Code: " << errorCode << " Msg: " << errorString;
    STX_LOGE(logger, oss.str());
}

void RealTimeData::nextValidId(OrderId orderId) {
    nextOrderId = orderId;
    STX_LOGI(logger, "Next valid order ID: " + std::to_string(orderId));
}

void RealTimeData::aggregateMinuteData() {
    if (l1Prices.empty()) {
        STX_LOGW(logger, "L1 data is empty.");
        return;  // No data to aggregate
    }

    std::lock_guard<std::mutex> lock(dataMutex);

    double open = l1Prices.front();
    double close = l1Prices.back();
    double high = *std::max_element(l1Prices.begin(), l1Prices.end());
    double low = *std::min_element(l1Prices.begin(), l1Prices.end());
    double volume = std::accumulate(l1Volumes.begin(), l1Volumes.end(), 0.0);

    double bidAskSpread = 0.0, midpoint = 0.0, priceChange = 0.0;
    double totalL2Volume = 0.0;

    for (const auto &data : l2Data) {
        totalL2Volume += data.at("BidSize") + data.at("AskSize");
    }

    if (!l2Data.empty()) {
        bidAskSpread = l2Data.back().at("AskPrice") - l2Data.front().at("BidPrice");
        midpoint = (l2Data.front().at("BidPrice") + l2Data.back().at("AskPrice")) / 2.0;
    }

    double gap = open - yesterdayClose;
    yesterdayClose = close;  // Update for the next day

    double rsi = calculateRSI();
    double macd = calculateMACD();
    double vwap = calculateVWAP();

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    std::string datetime = oss.str();

    // Write combined data to shared memory
    std::stringstream combinedData;
    combinedData << datetime << "," << open << "," << high << "," << low << "," << close << "," << volume << ","
                 << bidAskSpread << "," << midpoint << "," << priceChange << "," << totalL2Volume << "," << gap << ","
                 << rsi << "," << macd << "," << vwap << "\n";

    // Write to shared memory
    writeToSharedMemory(combinedData.str());

    // Insert into TimescaleDB with the formatted datetime string
    if (timescaleDB->insertL1Data(datetime, {{"Bid", l1Prices.front()}, {"Ask", l1Prices.back()}, {"Last", close}, {"Open", open}, {"High", high}, {"Low", low}, {"Close", close}, {"Volume", volume}})) {
        STX_LOGI(logger, "L1 data written to db successfully: " + datetime);
    } else {
        STX_LOGE(logger, "L1 data write to db failed: " + datetime);
    }
    if (timescaleDB->insertL2Data(datetime, l2Data)) {
        STX_LOGI(logger, "L2 data written to db successfully: " + datetime);
    } else {
        STX_LOGE(logger, "L2 data write to db failed: " + datetime);
    }
    if (timescaleDB->insertFeatureData(datetime, {{"Gap", gap}, {"TodayOpen", open}, {"TotalL2Volume", totalL2Volume}, {"RSI", rsi}, {"MACD", macd}, {"VWAP", vwap}})) {
        STX_LOGI(logger, "Feature data written to db successfully: " + datetime);
    } else {
        STX_LOGE(logger, "Feature data write to db failed: " + datetime);
    }

    // Clear temporary data
    l1Prices.clear();
    l1Volumes.clear();
    l2Data.clear();
}

void RealTimeData::writeToSharedMemory(const std::string &data) {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::memset(region.get_address(), 0, region.get_size());  // Clear shared memory before writing new data
    std::memcpy(region.get_address(), data.c_str(), data.size());
}

double RealTimeData::calculateRSI() {
    if (l1Prices.size() < 2) return 50.0; // Return neutral value if not enough data

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
    if (l1Prices.size() < 26) return 0.0;  // Not enough data

    double shortEMA = calculateEMA(12);
    double longEMA = calculateEMA(26);

    return shortEMA - longEMA;
}

double RealTimeData::calculateEMA(int period) {
    if (l1Prices.size() < period) return l1Prices.back();

    double multiplier = 2.0 / (period + 1);
    double ema = l1Prices[l1Prices.size() - period];  // Start with the first price in the period

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