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
    std::lock_guard<std::mutex> lock(connectionMutex);
    STX_LOGD(logger, "Acquired connectionMutex in connectToIB");

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        try {
            client = std::make_unique<EClientSocket>(this, osSignal.get());
            if (!client->eConnect(IB_HOST, IB_PORT, IB_CLIENT_ID, false)) {
                throw std::runtime_error("Failed to connect to IB TWS");
            }

            STX_LOGI(logger, "Connected to IB TWS.");

            reader = std::make_unique<EReader>(client.get(), osSignal.get());
            reader->start();

            readerThread = std::thread([this]() {
                while (client->isConnected()) {
                    osSignal->waitForSignal();
                    std::lock_guard<std::mutex> lock(readerMutex);
                    reader->processMsgs();
                }
            });

            connected = true;
            return true;
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Error during connectToIB: " + std::string(e.what()));
            if (attempt < maxRetries - 1) {
                STX_LOGI(logger, "Retrying connection in " + std::to_string(retryDelayMs) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            }
        }
    }

    return false;
}

bool RealTimeData::start() {
    STX_LOGD(logger, "Attempting to acquire clientMutex in start");
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        STX_LOGD(logger, "Acquired clientMutex in start");
        if (running) {
            STX_LOGI(logger, "RealTimeData is already running.");
            return true; // Already running, so consider it a success
        }
        STX_LOGI(logger, "Starting RealTimeData collection...");
        running = true;
    }

    if (!connected && !connectToIB()) {
        STX_LOGE(logger, "Failed to connect to IB TWS.");
        running = false; // Reset running flag
        return false;
    }

    try {
        initializeSharedMemory();
        requestData();

        processDataThread = std::thread(&RealTimeData::processData, this);
        monitorDataFlowThread = std::thread(&RealTimeData::monitorDataFlow, this, 3, 1000, 5000);

        STX_LOGI(logger, "RealTimeData collection started successfully.");
        return true;
    } catch (const std::exception &e) {
        STX_LOGE(logger, "Exception in start: " + std::string(e.what()));
        running = false; // Reset running flag
        return false;
    }
}

void RealTimeData::stop() {
    STX_LOGD(logger, "Attempting to acquire clientMutex in stop");
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        STX_LOGD(logger, "Acquired clientMutex in stop");
        if (!running) return;
        running = false;
    }

    STX_LOGD(logger, "Attempting to acquire connectionMutex in stop");
    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        STX_LOGD(logger, "Acquired connectionMutex in stop");
        if (client && client->isConnected()) {
            client->eDisconnect();
            STX_LOGI(logger, "Disconnected from IB TWS");
        }
    }

    {
        std::lock_guard<std::mutex> lock(cvMutex);
        cv.notify_all();
    }

    joinThreads();
    boost::interprocess::shared_memory_object::remove(SHARED_MEMORY_NAME);

    STX_LOGD(logger, "Attempting to acquire clientMutex in stop (cleanup)");
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        STX_LOGD(logger, "Acquired clientMutex in stop (cleanup)");
        client.reset();
        reader.reset();
        connected = false;
    }

    STX_LOGI(logger, "RealTimeData stopped and cleaned up.");
}

void RealTimeData::requestData(int maxRetries, int retryDelayMs) {
    Contract contract = createContract("SPY", "STK", "ARCA", "USD");
    STX_LOGD(logger, "Created contract: Symbol=" + contract.symbol + ", SecType=" + contract.secType + ", Exchange=" + contract.exchange + ", Currency=" + contract.currency);

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        try {
            int l1RequestId, l2RequestId;
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                l1RequestId = ++requestId;
                l2RequestId = ++requestId;
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
    client->reqMktDepth(l2RequestId, contract, 50, false, mktDepthOptionsPtr);
}

void RealTimeData::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib &attrib) {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (field == LAST) {
        l1Prices.push_back(price);
        STX_LOGD(logger, "Received tick price: {\"TickerId\": " + std::to_string(tickerId) + ", \"Price\": " + std::to_string(price) + "}");
    }
}

void RealTimeData::tickSize(TickerId tickerId, TickType field, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (field == LAST_SIZE) {
        l1Volumes.push_back(size);
        STX_LOGD(logger, "Received tick size: {\"TickerId\": " + std::to_string(tickerId) + ", \"Size\": " + DecimalFunctions::decimalToString(size) + "}");
    }
}

void RealTimeData::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);

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
            rawL2Data[position] = {0.0, 0, ""};
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
    STX_LOGI(logger, "Aggregating minute data. L1 Prices count: " + std::to_string(l1Prices.size()) + 
             ", L1 Volumes count: " + std::to_string(l1Volumes.size()) + 
             ", Raw L2 Data count: " + std::to_string(rawL2Data.size()));

    if (l1Prices.empty() || l1Volumes.empty() || rawL2Data.empty()) {
        STX_LOGW(logger, "Incomplete data. Clearing temporary data and skipping aggregation.");
        clearTemporaryData();
        return;
    }

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
            {"BuyVolume", DecimalFunctions::decimalToString(priceLevelBuckets[i].first)},
            {"SellVolume", DecimalFunctions::decimalToString(priceLevelBuckets[i].second)}
        };
        l2DataJson.push_back(level);
    }

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
        {"DepthChange", DecimalFunctions::decimalToString(depthChange)},
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

std::string RealTimeData::createCombinedJson(const std::string& datetime, const json& l1Data, const json& l2Data, const json& features) const {
    json combinedData = {
        {"datetime", datetime},
        {"L1", l1Data},
        {"L2", l2Data},
        {"Features", features}
    };
    return combinedData.dump();
}

void RealTimeData::clearTemporaryData() {
    STX_LOGI(logger, "Clearing temporary data. L1 Prices count: " + std::to_string(l1Prices.size()) + ", L1 Volumes count: " + std::to_string(l1Volumes.size()) + ", Raw L2 Data count: " + std::to_string(rawL2Data.size()));
    l1Prices.clear();
    l1Volumes.clear();
    rawL2Data.clear();
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
    STX_LOGW(logger, "Max number of requests exceeded, implementing backoff strategy.");
    
    static int backoffAttempt = 0;
    int delaySeconds = std::pow(2, backoffAttempt);
    
    if (delaySeconds > 300) {
        delaySeconds = 300;
    }
    
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
    running = false;
}

void RealTimeData::checkDataHealth() {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (rawL2Data.empty()) {
        STX_LOGW(logger, "L2 data is empty. Please ensure that the data request is successful.");
    } else {
        STX_LOGI(logger, "L2 data is present. Number of entries: " + std::to_string(rawL2Data.size()));
    }
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
    while (running) {
        auto now = std::chrono::system_clock::now();
        auto nextMinute = std::chrono::time_point_cast<std::chrono::minutes>(now) + std::chrono::minutes(1);
        std::this_thread::sleep_until(nextMinute);
        aggregateMinuteData();
        checkDataHealth();
    }
}

void RealTimeData::monitorDataFlow(int maxRetries, int retryDelayMs, int checkIntervalMs) {
    while (running) {
        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            if (!client || !client->isConnected()) {
                STX_LOGW(logger, "Connection lost. Attempting to reconnect...");
                if (!connectToIB(maxRetries, retryDelayMs)) {
                    STX_LOGE(logger, "Failed to reconnect to IB TWS.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
                    continue;
                }
                requestData(maxRetries, retryDelayMs);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    }
}

void RealTimeData::joinThreads() {
    processDataThread.join();
    STX_LOGI(logger, "processDataThread joined successfully");

    monitorDataFlowThread.join();
    STX_LOGI(logger, "monitorDataFlowThread joined successfully");

    readerThread.join();
    STX_LOGI(logger, "readerThread joined successfully");    
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