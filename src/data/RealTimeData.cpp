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
#include <future>
#include <boost/circular_buffer.hpp>
#include "RealTimeData.hpp"

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

    if (!logger || !db) {
        throw std::runtime_error("Logger or TimescaleDB is null");
    }
    STX_LOGI(logger, "RealTimeData object created successfully.");
}

RealTimeData::~RealTimeData() {
    stop(); 
}

bool RealTimeData::connectToIB(int maxRetries, int retryDelayMs) {
    STX_LOGD(logger, "Attempting to acquire connectionMutex in connectToIB");
    std::unique_lock<std::mutex> connectionLock(connectionMutex, std::defer_lock);
    connectionLock.lock();
    STX_LOGD(logger, "Acquired connectionMutex in connectToIB");

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        try {
            client = std::make_unique<EClientSocket>(this, osSignal.get());
            if (!client->eConnect(IB_HOST, IB_PORT, IB_CLIENT_ID, false)) {
                throw std::runtime_error("Failed to connect to IB TWS");
            }

            STX_LOGI(logger, "Connected to IB TWS.");

            reader = std::make_unique<EReader>(client.get(), osSignal.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            reader->start();

            readerThread = std::thread([this]() {
                while (running.load() && client->isConnected()) {
                    osSignal->waitForSignal();
                    std::unique_lock<std::mutex> readerLock(readerMutex, std::defer_lock);
                    readerLock.lock();
                    if (!running.load()) {
                        break;
                    }
                    reader->processMsgs();
                    readerLock.unlock();
                }
            });

            connected.store(true);
            connectionLock.unlock();
            return true;
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Error during connectToIB: " + std::string(e.what()));
            if (attempt < maxRetries - 1) {
                STX_LOGI(logger, "Retrying connection in " + std::to_string(retryDelayMs) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            }
        }
    }

    connectionLock.unlock();
    return false;
}

bool RealTimeData::start() {
    STX_LOGD(logger, "Attempting to acquire clientMutex in start");
    std::unique_lock<std::mutex> clientLock(clientMutex, std::defer_lock);
    clientLock.lock();
    STX_LOGD(logger, "Acquired clientMutex in start");
    if (running.load()) {
        STX_LOGI(logger, "RealTimeData is already running.");
        clientLock.unlock();
        return true; // Already running, so consider it a success
    }
    STX_LOGI(logger, "Starting RealTimeData collection...");
    running.store(true);
    clientLock.unlock();

    if (!connected.load() && !connectToIB()) {
        STX_LOGE(logger, "Failed to connect to IB TWS.");
        clientLock.lock();
        running.store(false);
        clientLock.unlock();
        return false;
    }

    try {
        initializeSharedMemory();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        requestData();

        processDataThread = std::thread(&RealTimeData::processData, this);
        monitorDataFlowThread = std::thread(&RealTimeData::monitorDataFlow, this, 3, 1000, 5000);

        STX_LOGI(logger, "RealTimeData collection started successfully.");
        return true;
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Exception in start: " + std::string(e.what()));
        clientLock.lock();
        running.store(false);
        clientLock.unlock();
        return false;
    }
}

void RealTimeData::stop() {
    
    running.store(false);

    // Notify all threads to exit immediately
    {
        std::lock_guard<std::mutex> lock(cvMutex);
        cv.notify_all();
    }
    
    if (client && client->isConnected()) {
        client->eDisconnect();
        STX_LOGI(logger, "Disconnected from IB TWS");
    }

    joinThreads();

    boost::interprocess::shared_memory_object::remove(SHARED_MEMORY_NAME);
    STX_LOGI(logger, "shared memory removed sussessfully.");

    if (client) {
        client.reset();
        STX_LOGI(logger, "client reset successfully.");
    } else {
        STX_LOGW(logger, "client was already nullptr.");
    }
    
    if (reader) {
        reader->stop();
        STX_LOGD(logger, "reader stopped successfully.");
    } else {
        STX_LOGW(logger, "reader was already null.");
    }
    
    connected.store(false);

    STX_LOGI(logger, "RealTimeData stopped and cleaned up.");
}

void RealTimeData::requestData(int maxRetries, int retryDelayMs) {
    Contract contract = createContract("SPY", "STK", "ARCA", "USD");
    STX_LOGD(logger, "Created contract: Symbol=" + contract.symbol + ", SecType=" + contract.secType + ", Exchange=" + contract.exchange + ", Currency=" + contract.currency);

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        try {
            int l1RequestId, l2RequestId;
            {
                std::unique_lock<std::mutex> clientLock(clientMutex, std::defer_lock);
                clientLock.lock();
                l1RequestId = ++requestId;
                l2RequestId = ++requestId;
                clientLock.unlock();
            } // Release the mutex here

            std::thread l1Thread(&RealTimeData::requestL1Data, this, l1RequestId, std::ref(contract));
            std::thread l2Thread(&RealTimeData::requestL2Data, this, l2RequestId, std::ref(contract));

            l1Thread.join();
            l2Thread.join();

            return;
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Error during requestData: " + std::string(e.what()));
            if (attempt < maxRetries - 1) {
                STX_LOGW(logger, "Retrying data request in " + std::to_string(retryDelayMs) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            }
        }
    }
}

void RealTimeData::requestL1Data(int l1RequestId, const Contract& contract) {
    STX_LOGD(logger, "Requesting L1 data with request ID: " + std::to_string(l1RequestId));
    client->reqMktData(l1RequestId, contract, "", false, false, TagValueListSPtr());
}

void RealTimeData::requestL2Data(int l2RequestId, const Contract& contract) {
    STX_LOGD(logger, "Requesting L2 data with request ID: " + std::to_string(l2RequestId));
    TagValueListSPtr mktDepthOptionsPtr = std::make_shared<std::vector<std::shared_ptr<TagValue>>>();
    client->reqMktDepth(l2RequestId, contract, 60, false, mktDepthOptionsPtr);
}

void RealTimeData::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib &attrib) {
    if (field == LAST) {
        l1Prices.push_back(price);
        STX_LOGD(logger, "Received tick price: {\"TickerId\": " + std::to_string(tickerId) + ", \"Price\": " + std::to_string(price) + "}");
    }
}

void RealTimeData::tickSize(TickerId tickerId, TickType field, Decimal size) {
    if (field == LAST_SIZE) {
        l1Volumes.push_back(size);
        STX_LOGD(logger, "Received tick size: {\"TickerId\": " + std::to_string(tickerId) + ", \"Size\": " + DecimalFunctions::decimalToString(size) + "}");
    }
}

void RealTimeData::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    if (position >= rawL2Data.size()) {
        rawL2Data.resize(position + 1);
    }

    switch (operation) {
        case 0: // Insert
            rawL2Data.insert(rawL2Data.begin() + position, {price, size, side == 0 ? "Buy" : "Sell"});
            STX_LOGD(logger, "Market depth inserted: {\"TickerId\": " + std::to_string(id) + 
                     ", \"Position\": " + std::to_string(position) + 
                     ", \"Operation\": \"Insert\"" + 
                     ", \"Side\": " + (side == 0 ? "Buy" : "Sell") + 
                     ", \"Price\": " + std::to_string(price) + 
                     ", \"Size\": " + DecimalFunctions::decimalToString(size) + "}");
            break;
        case 1: // Update
            rawL2Data[position] = {price, size, side == 0 ? "Buy" : "Sell"};
            STX_LOGD(logger, "Market depth updated: {\"TickerId\": " + std::to_string(id) + 
                     ", \"Position\": " + std::to_string(position) + 
                     ", \"Operation\": \"Update\"" + 
                     ", \"Side\": " + (side == 0 ? "Buy" : "Sell") + 
                     ", \"Price\": " + std::to_string(price) + 
                     ", \"Size\": " + DecimalFunctions::decimalToString(size) + "}");
            break;
        case 2: // Delete
            rawL2Data[position] = {0.0, 0, "Deleted"};
            STX_LOGD(logger, "Market depth deleted: {\"TickerId\": " + std::to_string(id) + 
                     ", \"Position\": " + std::to_string(position) + 
                     ", \"Operation\": \"Delete\"" + 
                     ", \"Side\": " + (side == 0 ? "Buy" : "Sell") + 
                     ", \"Price\": " + std::to_string(price) + 
                     ", \"Size\": " + DecimalFunctions::decimalToString(size) + "}");
            break;
        default:
            STX_LOGW(logger, "Unknown operation in updateMktDepth: " + std::to_string(operation));
            break;
    }
}

void RealTimeData::aggregateMinuteData() {
    if (l1Prices.empty() || l1Volumes.empty() || rawL2Data.empty()) {
        STX_LOGW(logger, "Incomplete data. Clearing temporary data and skipping aggregation.");
        clearTemporaryData();
        return;
    }

    swapBuffers();
    STX_LOGI(logger, "Aggregating minute data. L1 Prices count: " + std::to_string(l1PricesBuffer.size()) + 
             ", L1 Volumes count: " + std::to_string(l1VolumesBuffer.size()) + 
             ", Raw L2 Data count: " + std::to_string(rawL2DataBuffer.size()));

    try {
        json l1Data, l2Data;
        
        auto l1Future = std::async(std::launch::async, [this]() {
            return this->aggregateL1Data();
        });

        auto l2Future = std::async(std::launch::async, [this]() {
            return this->aggregateL2Data();
        });

        l1Data = l1Future.get();
        l2Data = l2Future.get();

        clearBufferData();
        
        auto features = calculateFeatures(l1Data, l2Data);
        
        std::string datetime = getCurrentDateTime();

        if (!writeToDatabase(datetime, l1Data, l2Data, features)) {
            throw std::runtime_error("Failed to write data to database");
        }

        writeToSharedMemory(createCombinedJson(datetime, l1Data, l2Data, features));
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Error in aggregateMinuteData: " + std::string(e.what()));
    }
}

json RealTimeData::aggregateL1Data() {
    double open = l1PricesBuffer.front();
    double close = l1PricesBuffer.back();
    double high = *std::max_element(l1PricesBuffer.begin(), l1PricesBuffer.end());
    double low = *std::min_element(l1PricesBuffer.begin(), l1PricesBuffer.end());
    Decimal volume = std::accumulate(l1VolumesBuffer.begin(), l1VolumesBuffer.end(), Decimal(0), DecimalFunctions::add);
    
    std::string aggregateResult = "open: " + std::to_string(open) +
                                "  close: " + std::to_string(close) +
                                "  high: " + std::to_string(high) +
                                "  low: " + std::to_string(low) +
                                "  volume: " + DecimalFunctions::decimalToString(volume);

    STX_LOGD(logger, aggregateResult);

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

    for (const auto& data : rawL2DataBuffer) {
        if (data.side == "Deleted" || data.price == 0.0) continue; // Skip deleted entries
        minPrice = std::min(minPrice, data.price);
        maxPrice = std::max(maxPrice, data.price);
    }

    double interval = (maxPrice - minPrice) / 20;
    if (interval == 0.0) {
        STX_LOGE(logger, "Interval calculation failed due to identical min and max prices.");
        return json::array();
    }

    std::string aggregateResult = "minPrice: " + std::to_string(minPrice) + 
                                "  maxPrice: " + std::to_string(maxPrice) + 
                                "  interval: " + std::to_string(interval);

    STX_LOGD(logger, aggregateResult);

    json l2DataJson = json::array();
    std::vector<std::pair<Decimal, Decimal>> priceLevelBuckets(20, {0, 0});

    for (const auto& data : rawL2DataBuffer) {
        if (data.side == "Deleted" || data.price == 0.0) continue; // Skip deleted entries
        int bucketIndex = static_cast<int>((data.price - minPrice) / interval);
        bucketIndex = std::clamp(bucketIndex, 0, 19);

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
            {"BuyVolume", DecimalFunctions::decimalToDouble(priceLevelBuckets[i].first)},
            {"SellVolume", DecimalFunctions::decimalToDouble(priceLevelBuckets[i].second)}
        };
        l2DataJson.push_back(level);
    }

    STX_LOGD(logger, l2DataJson.dump());

    return l2DataJson;
}

json RealTimeData::calculateFeatures(const json& l1Data, const json& l2Data) {
    std::future<double> weightedAvgPriceFuture = std::async(std::launch::async, &RealTimeData::calculateWeightedAveragePrice, this);
    std::future<double> buySellRatioFuture = std::async(std::launch::async, &RealTimeData::calculateBuySellRatio, this);
    std::future<Decimal> depthChangeFuture = std::async(std::launch::async, &RealTimeData::calculateDepthChange, this);
    Decimal volume = DecimalFunctions::stringToDecimal(l1Data["Volume"].get<std::string>());
    std::future<double> impliedLiquidityFuture = std::async(std::launch::async, &RealTimeData::calculateImpliedLiquidity, this, DecimalFunctions::decimalToDouble(volume), l2Data.size());
    std::future<double> priceMomentumFuture = std::async(std::launch::async, &RealTimeData::calculatePriceMomentum, this);
    std::future<double> tradeDensityFuture = std::async(std::launch::async, &RealTimeData::calculateTradeDensity, this);
    std::future<double> rsiFuture = std::async(std::launch::async, &RealTimeData::calculateRSI, this);
    std::future<double> macdFuture = std::async(std::launch::async, &RealTimeData::calculateMACD, this);
    std::future<double> vwapFuture = std::async(std::launch::async, &RealTimeData::calculateVWAP, this);

    double weightedAvgPrice = weightedAvgPriceFuture.get();
    double buySellRatio = buySellRatioFuture.get();
    Decimal depthChange = depthChangeFuture.get();
    double impliedLiquidity = impliedLiquidityFuture.get();
    double priceMomentum = priceMomentumFuture.get();
    double tradeDensity = tradeDensityFuture.get();
    double rsi = rsiFuture.get();
    double macd = macdFuture.get();
    double vwap = vwapFuture.get();

    return {
        {"WeightedAvgPrice", weightedAvgPrice},
        {"BuySellRatio", buySellRatio},
        {"DepthChange", DecimalFunctions::decimalToDouble(depthChange)},
        {"ImpliedLiquidity", impliedLiquidity},
        {"PriceMomentum", priceMomentum},
        {"TradeDensity", tradeDensity},
        {"RSI", rsi},
        {"MACD", macd},
        {"VWAP", vwap}
    };
}

std::string RealTimeData::getCurrentDateTime() const {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

bool RealTimeData::writeToDatabase(const std::string& datetime, const json& l1Data, const json& l2Data, const json& features) {
    STX_LOGI(logger, "Simulated writing real-time data to database for datetime: " + datetime);
    return true;
    // return db->insertRealTimeData(datetime, l1Data, l2Data, features);
}

void RealTimeData::writeToSharedMemory(const std::string &data) {
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

std::string RealTimeData::createCombinedJson(const std::string& datetime, const json& l1Data, const json& l2Data, const json& features) const {
    json combinedData = {
        {"datetime", datetime},
        {"L1", l1Data},
        {"L2", l2Data},
        {"Features", features}
    };
    return combinedData.dump();
}

void RealTimeData::swapBuffers() {
    std::unique_lock<std::mutex> bufferLock(bufferMutex, std::defer_lock);
    std::unique_lock<std::mutex> dataLock(dataMutex, std::defer_lock);
    bufferLock.lock();
    dataLock.lock();
    std::swap(l1Prices, l1PricesBuffer);
    std::swap(l1Volumes, l1VolumesBuffer);
    std::swap(rawL2Data, rawL2DataBuffer);
    bufferLock.unlock();
    dataLock.unlock();
}

void RealTimeData::clearBufferData() {
    std::unique_lock<std::mutex> bufferLock(bufferMutex, std::defer_lock);
    bufferLock.lock();
    STX_LOGI(logger, "Clearing buffer data. L1 Prices count: " + std::to_string(l1PricesBuffer.size()) + ", L1 Volumes count: " + std::to_string(l1VolumesBuffer.size()) + ", Raw L2 Data count: " + std::to_string(rawL2DataBuffer.size()));
    l1PricesBuffer.clear();
    l1VolumesBuffer.clear();
    rawL2DataBuffer.clear();
    bufferLock.unlock();
}

void RealTimeData::clearTemporaryData() {
    std::unique_lock<std::mutex> dataLock(dataMutex, std::defer_lock);
    dataLock.lock();
    STX_LOGI(logger, "Clearing temporary data. L1 Prices count: " + std::to_string(l1Prices.size()) + ", L1 Volumes count: " + std::to_string(l1Volumes.size()) + ", Raw L2 Data count: " + std::to_string(rawL2Data.size()));
    l1Prices.clear();
    l1Volumes.clear();
    rawL2Data.clear();
    dataLock.unlock();
}

double RealTimeData::calculateWeightedAveragePrice() const {
    Decimal totalWeightedPrice = 0;
    Decimal totalVolume = 0;
    for (size_t i = 0; i < l1Prices.size(); ++i) {
        totalWeightedPrice = DecimalFunctions::add(totalWeightedPrice, DecimalFunctions::mul(DecimalFunctions::doubleToDecimal(l1Prices[i]), l1Volumes[i]));
        totalVolume = DecimalFunctions::add(totalVolume, l1Volumes[i]);
    }
    return totalVolume == 0 ? 0.0 : DecimalFunctions::decimalToDouble(DecimalFunctions::div(totalWeightedPrice, totalVolume));
}

double RealTimeData::calculateBuySellRatio() const {
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

Decimal RealTimeData::calculateDepthChange() const {
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

double RealTimeData::calculateImpliedLiquidity(double totalL2Volume, size_t priceLevelCount) const {
    return totalL2Volume / (priceLevelCount + 1e-6);
}

double RealTimeData::calculatePriceMomentum() const {
    if (l1Prices.size() < 2) return 0.0;
    return l1Prices.back() - l1Prices.front();
}

double RealTimeData::calculateTradeDensity() const {
    if (l1Volumes.empty()) return 0.0;
    Decimal totalVolume = std::accumulate(l1Volumes.begin(), l1Volumes.end(), Decimal(0), DecimalFunctions::add);
    return DecimalFunctions::decimalToDouble(DecimalFunctions::div(totalVolume, DecimalFunctions::doubleToDecimal(l1Volumes.size())));
}

double RealTimeData::calculateRSI() const {
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

double RealTimeData::calculateMACD() const {
    if (l1Prices.size() < 26) return 0.0;

    double shortEMA = calculateEMA(12);
    double longEMA = calculateEMA(26);

    return shortEMA - longEMA;
}

double RealTimeData::calculateEMA(int period) const {
    if (l1Prices.size() < period) return l1Prices.back();

    double multiplier = 2.0 / (period + 1);
    double ema = l1Prices[l1Prices.size() - period];

    for (size_t i = l1Prices.size() - period + 1; i < l1Prices.size(); ++i) {
        ema = (l1Prices[i] - ema) * multiplier + ema;
    }

    return ema;
}

double RealTimeData::calculateVWAP() const {
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
    STX_LOGW(logger, "IB API Error: ID=" + std::to_string(id) + ", Code=" + std::to_string(errorCode) + ", Message=" + errorString);
    
    if (!advancedOrderRejectJson.empty()) {
        STX_LOGW(logger, "Advanced Order Reject JSON: " + advancedOrderRejectJson);
    }

    switch (errorCode) {
        case 10090:
            STX_LOGE(logger, "Market data subscription required for symbol. Check your IB account permissions.");
            break;
        case 200:
            STX_LOGE(logger, "Invalid contract specification. Check the contract details.");
            break;
        case 1100:
        case 1101:
        case 1102:
            handleConnectionError(errorCode);
            break;
        case 2104:
        case 2106:
            STX_LOGI(logger, "Data farm connection restored: " + errorString);
            break;
        case 2105:
        case 2107:
            STX_LOGW(logger, "Data farm connection lost: " + errorString);
            break;
        case 509:
            handleRateLimitExceeded();
            break;
        case 2152:
            STX_LOGE(logger, "Additional market data permissions required. Check your IB account permissions.");
            break;
        case 322:
            STX_LOGE(logger, "Duplicate ticker id. Ensure unique ticker ids for each request.");
            break;
        case 504:
            STX_LOGE(logger, "Not connected. Attempting to reconnect...");
            reconnect();
            break;
        default:
            STX_LOGW(logger, "Unhandled error code: " + std::to_string(errorCode) + ", additional info: " + advancedOrderRejectJson);
            break;
    }
}

void RealTimeData::handleConnectionError(int errorCode) {
    if (errorCode == 1102) {
        STX_LOGI(logger, "IB TWS reconnected successfully.");
        requestData();
    } else {
        STX_LOGE(logger, "IB TWS connection issue, attempting to reconnect...");
        reconnect();
    }
}

void RealTimeData::handleRateLimitExceeded() {
    static int backoffAttempt = 0;
    int delaySeconds = std::pow(2, backoffAttempt);

    if (delaySeconds > 300) {
        delaySeconds = 300; // Cap the delay to a maximum of 5 minutes
    }

    STX_LOGW(logger, "Max number of requests exceeded, implementing backoff strategy.");
    STX_LOGI(logger, "Backing off for " + std::to_string(delaySeconds) + " seconds before next request.");
    std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));

    backoffAttempt++;
}

void RealTimeData::reconnect() {
    STX_LOGI(logger, "Attempting to reconnect to IB TWS...");

    stop();

    int attempts = 0;
    const int max_attempts = 5;
    const int base_delay = 1;

    while (attempts < max_attempts) {
        if (connectToIB()) {
            STX_LOGI(logger, "Reconnected to IB TWS successfully.");
            start();
            return;
        } else {
            int delay = base_delay * std::pow(2, attempts);
            STX_LOGE(logger, "Reconnection attempt " + std::to_string(attempts + 1) + " failed. Retrying in " + std::to_string(delay) + " seconds.");
            ++attempts;
            std::this_thread::sleep_for(std::chrono::seconds(delay));
        }
    }

    STX_LOGE(logger, "Failed to reconnect to IB TWS after " + std::to_string(max_attempts) + " attempts.");
    running.store(false);
}

void RealTimeData::initializeSharedMemory() {
    STX_LOGI(logger, "Initializing shared memory...");
    boost::interprocess::shared_memory_object::remove(SHARED_MEMORY_NAME);
    shm = boost::interprocess::shared_memory_object(boost::interprocess::create_only, SHARED_MEMORY_NAME, boost::interprocess::read_write);
    shm.truncate(SHARED_MEMORY_SIZE);
    region = boost::interprocess::mapped_region(shm, boost::interprocess::read_write);
    STX_LOGI(logger, "Shared memory initialized successfully.");
}

void RealTimeData::processData() {
    while (running.load()) {
        auto now = std::chrono::system_clock::now();
        auto nextMinute = std::chrono::time_point_cast<std::chrono::minutes>(now) + std::chrono::minutes(1);
        std::unique_lock<std::mutex> lock(cvMutex);
        if (cv.wait_until(lock, nextMinute, [this] { return !running.load(); })) {
            break; // Exit if stop() was called
        }
        aggregateMinuteData();
    }
}

void RealTimeData::monitorDataFlow(int maxRetries, int retryDelayMs, int checkIntervalMs) {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
        std::unique_lock<std::mutex> lock(cvMutex);
        if (cv.wait_for(lock, std::chrono::milliseconds(checkIntervalMs), [this] { return !running.load(); })) {
            break; // Exit if stop() was called
        }
        lock.unlock();
        
        if (!client || !client->isConnected()) {
            STX_LOGW(logger, "Connection lost. Attempting to reconnect...");
            if (!connectToIB(maxRetries, retryDelayMs)) {
                STX_LOGE(logger, "Failed to reconnect to IB TWS.");
                continue;
            }
            requestData(maxRetries, retryDelayMs);
        }
    }
}

void RealTimeData::joinThreads() {
    if (processDataThread.joinable()) {
        processDataThread.join();
        STX_LOGI(logger, "processDataThread joined successfully");
    }
    if (monitorDataFlowThread.joinable()) {
        monitorDataFlowThread.join();
        STX_LOGI(logger, "monitorDataFlowThread joined successfully");
    }
    if (readerThread.joinable()) {
        readerThread.join();
        STX_LOGI(logger, "readerThread joined successfully");    
    }
}

Contract RealTimeData::createContract(const std::string& symbol, const std::string& secType, const std::string& exchange, const std::string& currency) {
    Contract contract;
    contract.symbol = symbol;
    contract.secType = secType;
    contract.exchange = exchange;
    contract.primaryExchange = exchange;
    contract.currency = currency;
    return contract;
}