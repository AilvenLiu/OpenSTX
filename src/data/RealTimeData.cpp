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
#include <boost/circular_buffer.hpp>
#include "RealTimeData.h"

using json = nlohmann::json;

constexpr const char* IB_HOST = "127.0.0.1";
constexpr int IB_PORT = 7496;
constexpr int IB_CLIENT_ID = 0;
constexpr const char* SHARED_MEMORY_NAME = "RealTimeData";
constexpr size_t SHARED_MEMORY_SIZE = 4096;

RealTimeData::RealTimeData(const std::shared_ptr<Logger>& log, const std::shared_ptr<TimescaleDB>& _db)
    : logger(log), db(_db), 
      osSignal(std::make_unique<EReaderOSSignal>(2000)), 
      client(nullptr), 
      reader(nullptr),
      nextOrderId(0), 
      requestId(0), yesterdayClose(0.0), running(false), previousVolume(0), connected(false) {

    if (!logger) {
        throw std::runtime_error("Logger is null");
    }
    if (!db) {
        STX_LOGE(logger, "TimescaleDB is null");
        throw std::runtime_error("TimescaleDB is null");
    }
    STX_LOGI(logger, "RealTimeData object created successfully.");
}

RealTimeData::~RealTimeData() {
    stop(); 
}

bool RealTimeData::connectToIB() {
    std::lock_guard<std::mutex> lock(clientMutex);

    try {
        client = std::make_unique<EClientSocket>(this, osSignal.get());
        if (!client) {
            throw std::runtime_error("Failed to create EClientSocket");
        }

        if (!client->eConnect(IB_HOST, IB_PORT, IB_CLIENT_ID, false)) {
            throw std::runtime_error("Failed to connect to IB TWS");
        }

        STX_LOGI(logger, "Connected to IB TWS.");

        reader = std::make_unique<EReader>(client.get(), osSignal.get());
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
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error during connectToIB: " + std::string(e.what()));
        stop();
        return false;
    }
}

void RealTimeData::start() {
    try {
        if (!connected) {
            if (connectToIB()) {
                STX_LOGI(logger, "RealTimeData object created and connected successfully.");
            } else {
                STX_LOGE(logger, "Failed to connect to IB TWS.");
                throw std::runtime_error("Connection to IB TWS failed during initialization.");
            }
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error in RealTimeData constructor: " + std::string(e.what()));
        stop(); 
        throw;
    }

    running = true;

    try {
        boost::interprocess::shared_memory_object::remove(SHARED_MEMORY_NAME);

        shm = boost::interprocess::shared_memory_object(boost::interprocess::create_only, SHARED_MEMORY_NAME, boost::interprocess::read_write);
        shm.truncate(SHARED_MEMORY_SIZE);
        region = boost::interprocess::mapped_region(shm, boost::interprocess::read_write);
        STX_LOGI(logger, "Shared memory RealTimeData created successfully.");

        if (connected) {
            requestData();

            processDataThread = std::thread([&]() {
                while (running) {
                    auto now = std::chrono::system_clock::now();
                    auto nextMinute = std::chrono::time_point_cast<std::chrono::minutes>(now) + std::chrono::minutes(1);

                    std::this_thread::sleep_until(nextMinute);

                    aggregateMinuteData();
                }
            });
        } else {
            STX_LOGE(logger, "Cannot start data processing as IB TWS connection is not established.");
            stop(); // 如果连接未建立，停止处理
        }
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Exception in start: " + std::string(e.what()));
        stop(); // 处理异常并清理资源
    }
}

void RealTimeData::stop() {
    if (!running) return;

    running = false;

    if (processDataThread.joinable()) {
        processDataThread.join();
        STX_LOGI(logger, "processDataThread joined successfully");
    }
    if (readerThread.joinable()) {
        readerThread.join();
        STX_LOGI(logger, "readerThread joined successfully");
    }

    boost::interprocess::shared_memory_object::remove(SHARED_MEMORY_NAME);

    {
        std::lock_guard<std::mutex> lock(clientMutex);
        if (client && client->isConnected()) {
            client->eDisconnect();
            STX_LOGI(logger, "Disconnected from IB TWS");
        }
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
    client->reqMktDepth(l2RequestId, contract, 50, true, TagValueListSPtr());
    
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
                     ", Size " + DecimalFunctions::decimalToString(size));
}

void RealTimeData::processL2Data(int position, double price, Decimal size, int side) {
    if (side == 1) {  // 买单
        rawL2Data.push_back({price, size, "Buy"});
    } else {  // 卖单
        rawL2Data.push_back({price, size, "Sell"});
    }
}

void RealTimeData::aggregateMinuteData() {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (l1Prices.empty() || rawL2Data.empty()) {
        STX_LOGE(logger, "Empty data. Skipping aggregation.");
        return;
    }

    try {
        auto l1Data = aggregateL1Data();
        auto l2Data = aggregateL2Data();
        auto features = calculateFeatures(l1Data, l2Data);

        std::string datetime = getCurrentDateTime();

        if (!writeToDatabase(datetime, l1Data, l2Data, features)) {
            throw std::runtime_error("Failed to write data to database");
        }

        writeToSharedMemory(createCombinedJson(datetime, l1Data, l2Data, features));

        clearTemporaryData();
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error in aggregateMinuteData: " + std::string(e.what()));
    }
}

json RealTimeData::aggregateL1Data() {
    double open = l1Prices.front();
    double close = l1Prices.back();
    double high = *std::max_element(l1Prices.begin(), l1Prices.end());
    double low = *std::min_element(l1Prices.begin(), l1Prices.end());
    Decimal volume = DecimalFunctions::sub(l1Volumes.back(), previousVolume);
    previousVolume = l1Volumes.back();

    return {
        {"Open", open},
        {"High", high},
        {"Low", low},
        {"Close", close},
        {"Volume", DecimalFunctions::decimalToString(volume)}
    };
}

json RealTimeData::aggregateL2Data() {
    double minPrice = std::numeric_limits<double>::max();
    double maxPrice = std::numeric_limits<double>::lowest();

    for (const auto& data : rawL2Data) {
        minPrice = std::min(minPrice, data.price);
        maxPrice = std::max(maxPrice, data.price);
    }

    double interval = (maxPrice - minPrice) / 20;
    if (interval == 0.0) {
        STX_LOGE(logger, "Interval calculation failed due to identical min and max prices.");
        return json::array();
    }

    json l2DataJson = json::array();

    std::vector<std::pair<Decimal, Decimal>> priceLevelBuckets(20, {0, 0});

    for (const auto& data : rawL2Data) {
        int bucketIndex = static_cast<int>((data.price - minPrice) / interval);
        bucketIndex = std::clamp(bucketIndex, 0, 19); // 防止越界

        if (data.side == "Buy") {
            priceLevelBuckets[bucketIndex].first = DecimalFunctions::add(priceLevelBuckets[bucketIndex].first, data.volume);
        } else {
            priceLevelBuckets[bucketIndex].second = DecimalFunctions::add(priceLevelBuckets[bucketIndex].second, data.volume);
        }
    }

    for (int i = 0; i < 20; ++i) {
        double midPrice = minPrice + (i + 0.5) * interval;
        json level = {
            {"Price", midPrice},
            {"BuyVolume", DecimalFunctions::decimalToString(priceLevelBuckets[i].first)},
            {"SellVolume", DecimalFunctions::decimalToString(priceLevelBuckets[i].second)}
        };
        l2DataJson.push_back(level);
    }

    return l2DataJson;
}

json RealTimeData::calculateFeatures(const json& l1Data, const json& l2Data) {
    double weightedAvgPrice = calculateWeightedAveragePrice();
    double buySellRatio = calculateBuySellRatio();
    Decimal depthChange = calculateDepthChange();
    Decimal volume = DecimalFunctions::stringToDecimal(l1Data["Volume"].get<std::string>());
    double impliedLiquidity = calculateImpliedLiquidity(volume, l2Data.size());
    double priceMomentum = calculatePriceMomentum();
    double tradeDensity = calculateTradeDensity();
    double rsi = calculateRSI();
    double macd = calculateMACD();
    double vwap = calculateVWAP();

    return {
        {"WeightedAvgPrice", weightedAvgPrice},
        {"BuySellRatio", buySellRatio},
        {"DepthChange", DecimalFunctions::decimalToString(depthChange)},
        {"ImpliedLiquidity", impliedLiquidity},
        {"PriceMomentum", priceMomentum},
        {"TradeDensity", tradeDensity},
        {"RSI", rsi},
        {"MACD", macd},
        {"VWAP", vwap}
    };
}

std::string RealTimeData::getCurrentDateTime() {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

bool RealTimeData::writeToDatabase(const std::string& datetime, const json& l1Data, const json& l2Data, const json& features) {
    return db->insertRealTimeData(datetime, l1Data, l2Data, features);
}

void RealTimeData::writeToSharedMemory(const std::string &data) {
    std::lock_guard<std::mutex> lock(dataMutex);
    try {
        if (data.size() > region.get_size()) {
            throw std::runtime_error("Data size exceeds shared memory size");
        }
        std::memset(region.get_address(), 0, region.get_size());
        std::memcpy(region.get_address(), data.c_str(), data.size());
        STX_LOGI(logger, "Data written to shared memory");
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error writing to shared memory: " + std::string(e.what()));
    }
}

std::string RealTimeData::createCombinedJson(const std::string& datetime, const json& l1Data, const json& l2Data, const json& features) {
    json combinedData = {
        {"datetime", datetime},
        {"L1", l1Data},
        {"L2", l2Data},
        {"Features", features}
    };
    return combinedData.dump();
}

void RealTimeData::clearTemporaryData() {
    l1Prices.clear();
    l1Volumes.clear();
    rawL2Data.clear();
}

// 各种指标计算的具体实现
double RealTimeData::calculateWeightedAveragePrice() {
    Decimal totalWeightedPrice = 0;
    Decimal totalVolume = 0;
    for (size_t i = 0; i < l1Prices.size(); ++i) {
        totalWeightedPrice = DecimalFunctions::add(totalWeightedPrice, DecimalFunctions::mul(DecimalFunctions::doubleToDecimal(l1Prices[i]), l1Volumes[i]));
        totalVolume = DecimalFunctions::add(totalVolume, l1Volumes[i]);
    }
    return totalVolume == 0 ? 0.0 : DecimalFunctions::decimalToDouble(DecimalFunctions::div(totalWeightedPrice, totalVolume));
}

double RealTimeData::calculateBuySellRatio() {
    Decimal totalBuyVolume = 0;
    Decimal totalSellVolume = 0;
    for (const auto& level : rawL2Data) {
        if (level.side == "Buy") {
            totalBuyVolume = DecimalFunctions::add(totalBuyVolume, level.volume);
        } else {
            totalSellVolume = DecimalFunctions::add(totalSellVolume, level.volume);
        }
    }
    return totalSellVolume == 0 ? 0.0 : DecimalFunctions::decimalToDouble(DecimalFunctions::div(totalBuyVolume, totalSellVolume));
}

Decimal RealTimeData::calculateDepthChange() {
    Decimal totalBuyVolume = 0;
    Decimal totalSellVolume = 0;

    for (const auto& level : rawL2Data) {
        if (level.side == "Buy") {
            totalBuyVolume = DecimalFunctions::add(totalBuyVolume, level.volume);
        } else if (level.side == "Sell") {
            totalSellVolume = DecimalFunctions::add(totalSellVolume, level.volume);
        }
    }

    return DecimalFunctions::sub(totalBuyVolume, totalSellVolume);
}

double RealTimeData::calculateImpliedLiquidity(const Decimal& totalL2Volume, size_t priceLevelCount) {
    return DecimalFunctions::decimalToDouble(DecimalFunctions::div(totalL2Volume, DecimalFunctions::doubleToDecimal(priceLevelCount + 1e-6)));
}

double RealTimeData::calculatePriceMomentum() {
    if (l1Prices.size() < 2) return 0.0;
    return l1Prices.back() - l1Prices.front();
}

double RealTimeData::calculateTradeDensity() {
    return l1Volumes.size();
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

void RealTimeData::nextValidId(OrderId orderId) {
    if (orderId <= 0) {
        STX_LOGE(logger, "Received an invalid order ID: " + std::to_string(orderId));
        return;
    }

    nextOrderId = orderId; 
    STX_LOGI(logger, "Next valid order ID received: " + std::to_string(orderId));
}

void RealTimeData::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    STX_LOGE(logger, "Error occurred: ID=" + std::to_string(id) + ", Code=" + std::to_string(errorCode) + ", Message=" + errorString);

    switch (errorCode) {
        case 1100: // 连接丢失
            STX_LOGE(logger, "IB TWS connection lost, attempting to reconnect...");
            reconnect();
            break;
        case 1101: // 连接重置
            STX_LOGE(logger, "IB TWS connection reset, attempting to reconnect...");
            reconnect();
            break;
        case 1102: // 重新连接成功
            STX_LOGI(logger, "IB TWS reconnected successfully.");
            break;
        case 509: // 阻塞请求超限
            STX_LOGW(logger, "Max number of requests exceeded, consider reducing request frequency.");
            break;
        default:
            STX_LOGW(logger, "Unhandled error code: " + std::to_string(errorCode) + ", additional info: " + advancedOrderRejectJson);
            break;
    }
}

// reconnect 方法的实现
void RealTimeData::reconnect() {
    STX_LOGI(logger, "Attempting to reconnect to IB TWS...");

    stop();

    int attempts = 0;
    const int max_attempts = 5;
    const int base_delay = 1;

    while (attempts < max_attempts) {
        if (connectToIB()) {
            STX_LOGI(logger, "Reconnected to IB TWS successfully.");
            return;
        } else {
            int delay = base_delay * std::pow(2, attempts);
            STX_LOGE(logger, "Reconnection attempt " + std::to_string(attempts + 1) + " failed. Retrying in " + std::to_string(delay) + " seconds.");
        }
        ++attempts;
        std::this_thread::sleep_for(std::chrono::seconds(delay));
    }

    STX_LOGE(logger, "Failed to reconnect to IB TWS after " + std::to_string(max_attempts) + " attempts.");
    running = false;
}