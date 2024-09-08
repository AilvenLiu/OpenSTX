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

#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "DailyDataFetcher.hpp"

constexpr const char* IB_HOST = "127.0.0.1";
constexpr int IB_PORT = 7496;
constexpr int IB_CLIENT_ID = 2;

DailyDataFetcher::DailyDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& _db)
    : logger(logger), db(_db), 
      osSignal(nullptr),
      client(nullptr),
      reader(nullptr), 
      running(false), 
      dataReceived(false), nextRequestId(0), m_nextValidId(0) {
    
    if (!logger) {
        throw std::runtime_error("Logger is null");
    }
    if (!db) {
        STX_LOGE(logger, "TimescaleDB is null");
        throw std::runtime_error("TimescaleDB is null");
    }

    STX_LOGI(logger, "DailyDataFetcher object created successfully.");
}

DailyDataFetcher::~DailyDataFetcher() {
    if (running.load()) stop();
}

bool DailyDataFetcher::connectToIB(int maxRetries, int retryDelayMs) {
    std::unique_lock<std::mutex> clientLock(clientMutex, std::defer_lock);
    clientLock.lock();

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        try {
            if (!osSignal) osSignal = std::make_unique<EReaderOSSignal>(2000);
            if (!osSignal) throw std::runtime_error("Failed to create EReaderOSSignal");

            if (!client) client = std::make_unique<EClientSocket>(this, osSignal.get());
            if (!client) throw std::runtime_error("Failed to create EClientSocket");

            if (!client->eConnect(IB_HOST, IB_PORT, IB_CLIENT_ID, false)) {
                throw std::runtime_error("Failed to connect to IB TWS");
            }

            if (!reader) reader = std::make_unique<EReader>(client.get(), osSignal.get());
            if (!reader) throw std::runtime_error("Failed to create EReader");
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

            std::unique_lock<std::mutex> nextIdLock(nextValidIdMutex, std::defer_lock);
            nextIdLock.lock();
            STX_LOGI(logger, "Waiting for next valid ID...");
            auto waitResult = nextValidIdCV.wait_for(nextIdLock, std::chrono::seconds(30),  
                [this] { return m_nextValidId > 0; });
            nextIdLock.unlock();

            if (!waitResult) {
                STX_LOGE(logger, "Timeout waiting for next valid ID. Current m_nextValidId: " + std::to_string(m_nextValidId));
            } else {
                STX_LOGI(logger, "Connected to IB TWS.");
                clientLock.unlock();
                return true; 
            }
        } catch (const std::exception &e) {
            STX_LOGE(logger, "Error during connectToIB: " + std::string(e.what()));
            if (attempt < maxRetries - 1) {
                STX_LOGI(logger, "Retrying connection in " + std::to_string(retryDelayMs) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            } else {
                // Cleanup resources on final failure
                if (client && client->isConnected()) {
                    client->eDisconnect();
                }
                client.reset();
                reader.reset();
                osSignal.reset();
            }
        }
    }
    
    clientLock.unlock();
    return false;
}

void DailyDataFetcher::stop() {
    if (!running.load()) {
        STX_LOGW(logger, "DailyDataFetcher is already stopped.");
        return;
    }

    running.store(false);

    {
        std::lock_guard<std::mutex> cvLock(cvMutex);
        std::lock_guard<std::mutex> nextValidIdCVLock(nextValidIdMutex);
        std::lock_guard<std::mutex> queueLock(queueMutex);
        cv.notify_all();
        nextValidIdCV.notify_all();
        queueCV.notify_all();
    }

    if (databaseThread.joinable()) {
        databaseThread.join();
        STX_LOGI(logger, "databaseThread joined successfully.");
    }
    if (readerThread.joinable()) {
        readerThread.join();
        STX_LOGI(logger, "readerThread joined successfully.");
    }

    std::unique_lock<std::mutex> clientLock(clientMutex, std::defer_lock);
    clientLock.lock();
    if (client && client->isConnected()) {
        client->eDisconnect();
        STX_LOGI(logger, "Disconnected from IB TWS.");
    }
    clientLock.unlock();

    if (client) {
        client.reset();
        STX_LOGI(logger, "client reset successfully.");
    } else {
        STX_LOGW(logger, "client was already nullptr.");
    }

    if (reader) {
        reader->stop();
        STX_LOGI(logger, "reader stopped successfully.");
    } else {
        STX_LOGW(logger, "reader was already null.");
    }

    if (osSignal) {
        osSignal.reset();
        STX_LOGI(logger, "osSignal reset successfully.");
    } else {
        STX_LOGW(logger, "osSignal was already null.");
    }

    STX_LOGI(logger, "DailyDataFetcher stopped and cleaned up.");
}

bool DailyDataFetcher::fetchAndProcessDailyData(const std::string& symbol, const std::string& duration, bool incremental) {
    if (databaseThread.joinable()) {
        databaseThread.join();
    }
    databaseThread = std::thread(&DailyDataFetcher::writeToDatabaseFunc, this);

    std::unique_lock<std::mutex> clientLock(clientMutex, std::defer_lock);
    clientLock.lock();
    if (running.load()) {
        STX_LOGW(logger, "DailyDataFetcher is already running.");
        clientLock.unlock();
        return true;
    }
    STX_LOGI(logger, "start DailyDataFetcher collection ...");
    running.store(true);
    clientLock.unlock();

    if (!connectToIB()) {
        STX_LOGE(logger, "Failed to connect to IB TWS.");
        running.store(false);
        return false;
    }

    STX_LOGI(logger, "Waiting for connection establishment for 5 seconds ...");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    STX_LOGI(logger, "Start to request daily data.");

    
    std::vector<std::string> symbols;
    if (symbol == "ALL") {
        symbols = {"SPY", "QQQ", "XLK", "AAPL", "MSFT", "AMZN", "GOOGL", "TSLA", "NVDA", "META", "AMD", "ADBE", "CRM", "SHOP"};
    } else {
        symbols.push_back(symbol);
    }

    bool success = true;
    int retryCount = 0;
    int maxRetryTimes = 5;
    while (retryCount <= maxRetryTimes) {
        for (const std::string& sym : symbols) {
            if (!running.load()) break;

            STX_LOGI(logger, "Fetching and processing historical data for symbol: " + sym);

            std::string endDateTime = getCurrentDate();
            std::string startDateTime;

            if (incremental) {
                std::string lastDate = db->getLastDailyEndDate(sym);
                std::string firstDate = db->getFirstDailyStartDate(sym);
                std::string tenYearsAgo = calculateStartDateFromDuration("10 Y");
                
                if (firstDate.empty()) {
                    startDateTime = tenYearsAgo;
                } else {
                    startDateTime = lastDate;
                }
            } else {
                startDateTime = calculateStartDateFromDuration(duration.empty() ? "10 Y" : duration);
            }

            auto dateRanges = splitDateRange(startDateTime, endDateTime);
            STX_LOGW(logger, "data to be requested: from " + dateRanges.front().first + " to " + dateRanges.back().second);

            for (const auto& range : dateRanges) {
                if (!running.load()) break;

                if (!requestAndProcessWeeklyData(sym, range.first, range.second)) {
                    success = false;
                    break;
                }

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

            STX_LOGI(logger, "Completed fetching and processing historical data for symbol: " + sym);
        }

        if (success) {
            break;
        } else {
            retryCount++;
            STX_LOGE(logger, "Failed to fetch data. Retry " + std::to_string(++retryCount) + " of " + std::to_string(maxRetryTimes));
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    STX_LOGI(logger, "Daily data has been requested totally, exit thread now...");
    stop();
    return true;
}

bool DailyDataFetcher::requestAndProcessWeeklyData(const std::string& symbol, const std::string& startDate, const std::string& endDate) {
    STX_LOGI(logger, "Requesting weekly data for " + symbol + " from " + startDate + " to " + endDate);

    const int maxRetries = 3;
    int retryCount = 0;
    bool success = false;

    while (retryCount < maxRetries) {
        if (requestDailyData(symbol, startDate, endDate, "1 day")) {
            success = true;
            break;
        } else {
            STX_LOGE(logger, "Failed to request daily data for " + symbol + " from " + startDate + " to " + endDate + ". Retry " + std::to_string(retryCount + 1) + " of " + std::to_string(maxRetries));
            retryCount++;
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait before retrying
        }
    }

    if (!success) {
        STX_LOGE(logger, "Failed to request daily data for " + symbol + " after " + std::to_string(maxRetries) + " retries.");
        return false;
    }

    for (const auto& data : historicalDataBuffer) {
        storeDailyData(symbol, data);
    }

    historicalDataBuffer.clear();
    return true;
}

bool DailyDataFetcher::requestDailyData(const std::string& symbol, const std::string& startDate, const std::string& endDate, const std::string& barSize) {
    if (!client || !client->isConnected()) {
        STX_LOGE(logger, "Not connected to IB TWS. Cannot request historical data.");
        return false;
    }

    Contract contract;
    contract.symbol = symbol;
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";

    std::string whatToShow = "TRADES";
    bool useRTH = true;
    int formatDate = 1;

    // Format the end date correctly
    std::string formattedEndDate = endDate + " 23:59:59 US/Eastern";

    // Calculate the duration
    int durationDays = calculateDurationInDays(startDate, endDate);
    std::string duration = std::to_string(durationDays) + " D";

    try {
        client->reqHistoricalData(nextRequestId++, contract, formattedEndDate, duration, barSize, whatToShow, useRTH, formatDate, false, TagValueListSPtr());
        return waitForData();
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Exception while requesting historical data: " + std::string(e.what()));
        return false;
    }
}

int DailyDataFetcher::calculateDurationInDays(const std::string& startDate, const std::string& endDate) {
    std::tm tm_start = {}, tm_end = {};
    std::istringstream ss_start(startDate), ss_end(endDate);
    ss_start >> std::get_time(&tm_start, "%Y-%m-%d");
    ss_end >> std::get_time(&tm_end, "%Y-%m-%d");
    
    std::time_t time_start = std::mktime(&tm_start);
    std::time_t time_end = std::mktime(&tm_end);
    
    return static_cast<int>(std::difftime(time_end, time_start) / (60 * 60 * 24)) + 1;
}

bool DailyDataFetcher::waitForData() {
    std::unique_lock<std::mutex> lock(cvMutex);
    if (!cv.wait_for(lock, std::chrono::seconds(30), [this] { return dataReceived || !running.load(); })) {
        STX_LOGE(logger, "Timeout waiting for historical data");
        return false;
    }
    if (!client->isConnected()) {
        STX_LOGE(logger, "Connection to IB lost while waiting for data");
        throw std::runtime_error("Connection to IB lost");
    }
    dataReceived = false;
    return true;
}

void DailyDataFetcher::historicalData(TickerId reqId, const Bar& bar) {
    std::map<std::string, std::variant<double, std::string>> data;
    data["date"] = bar.time;
    data["open"] = bar.open;
    data["high"] = bar.high;
    data["low"] = bar.low;
    data["close"] = bar.close;
    data["volume"] = DecimalFunctions::decimalToDouble(bar.volume);

    historicalDataBuffer.push_back(data);
}

void DailyDataFetcher::historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) {
    std::unique_lock<std::mutex> lock(cvMutex);
    dataReceived = true;
    cv.notify_one();
}

void DailyDataFetcher::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    STX_LOGE(logger, "Error: " + std::to_string(id) + " - " + std::to_string(errorCode) + " - " + errorString);

    if (errorCode == 509 || errorCode == 1100) { 
        std::lock_guard<std::mutex> lock(cvMutex);
        dataReceived = true;
        cv.notify_one();
        
        // Attempt to reconnect
        if (connectToIB()) {
            STX_LOGI(logger, "Successfully reconnected to IB TWS.");
        } else {
            STX_LOGE(logger, "Failed to reconnect to IB TWS.");
        }
    } else {
        std::unique_lock<std::mutex> lock(cvMutex);
        dataReceived = true;
        cv.notify_one();
        lock.unlock();
    }
}

void DailyDataFetcher::nextValidId(OrderId orderId) {
    std::lock_guard<std::mutex> lock(nextValidIdMutex);
    m_nextValidId = orderId;
    STX_LOGI(logger, "Received nextValidId: " + std::to_string(orderId));
    nextValidIdCV.notify_one();
}

void DailyDataFetcher::storeDailyData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    std::string date;
    try {
        date = std::get<std::string>(historicalData.at("date"));
    } catch (const std::out_of_range& e) {
        STX_LOGE(logger, "Missing 'date' field in historical data for symbol: " + symbol);
        return;
    }

    std::map<std::string, std::variant<double, std::string>> dbData;
    dbData["symbol"] = symbol;

    // Required fields from IB API
    const std::vector<std::string> requiredFields = {"open", "high", "low", "close", "volume"};
    for (const auto& field : requiredFields) {
        try {
            dbData[field] = std::get<double>(historicalData.at(field));
        } catch (const std::out_of_range& e) {
            STX_LOGE(logger, "Missing required field: " + field + " for " + symbol + " on " + date);
            return;  // Skip this data point if a required field is missing
        }
    }

    // Optional fields (calculated or may not be available)
    const std::vector<std::string> optionalFields = {"adj_close", "sma", "ema", "rsi", "macd", "vwap", "momentum"};
    for (const auto& field : optionalFields) {
        if (historicalData.find(field) != historicalData.end()) {
            dbData[field] = std::get<double>(historicalData.at(field));
        } else {
            // Use a default value or calculate if missing
            dbData[field] = 0.0;  // or calculate the value here
        }
    }

    // Calculate technical indicators if they're not provided
    double close = std::get<double>(dbData["close"]);
    double volume = std::get<double>(dbData["volume"]);

    if (std::get<double>(dbData["sma"]) == 0.0) dbData["sma"] = calculateSMA(symbol, close);
    if (std::get<double>(dbData["ema"]) == 0.0) dbData["ema"] = calculateEMA(symbol, close);
    if (std::get<double>(dbData["rsi"]) == 0.0) dbData["rsi"] = calculateRSI(symbol, close);
    if (std::get<double>(dbData["macd"]) == 0.0) dbData["macd"] = calculateMACD(symbol, close);
    if (std::get<double>(dbData["vwap"]) == 0.0) dbData["vwap"] = calculateVWAP(symbol, volume, close);
    if (std::get<double>(dbData["momentum"]) == 0.0) dbData["momentum"] = calculateMomentum(symbol, close);

    // If adj_close is not provided, use regular close
    if (std::get<double>(dbData["adj_close"]) == 0.0) dbData["adj_close"] = close;

    addToQueue(date, dbData);
}

std::vector<std::pair<std::string, std::string>> DailyDataFetcher::splitDateRange(const std::string& startDate, const std::string& endDate) {
    std::vector<std::pair<std::string, std::string>> dateRanges;
    std::tm startTm = {};
    std::tm endTm = {};

    std::istringstream ssStart(startDate);
    ssStart >> std::get_time(&startTm, "%Y-%m-%d");

    std::istringstream ssEnd(endDate);
    ssEnd >> std::get_time(&endTm, "%Y-%m-%d");

    while (std::difftime(std::mktime(&endTm), std::mktime(&startTm)) > 0) {
        std::tm nextTm = startTm;
        nextTm.tm_mday += 7;
        std::mktime(&nextTm);

        if (std::mktime(&nextTm) > std::mktime(&endTm)) {
            nextTm = endTm;
        }

        std::ostringstream ossStart, ossEnd;
        ossStart << std::put_time(&startTm, "%Y%m%d");
        ossEnd << std::put_time(&nextTm, "%Y%m%d");

        dateRanges.emplace_back(ossStart.str(), ossEnd.str());

        startTm = nextTm;
        std::mktime(&startTm);
    }

    return dateRanges;
}

std::string DailyDataFetcher::calculateStartDateFromDuration(const std::string& duration) {
    std::tm tm = {};
    std::time_t now = std::time(nullptr);
    tm = *std::localtime(&now);

    if (duration.find("Y") != std::string::npos) {
        int years = std::stoi(duration.substr(0, duration.find("Y")));
        tm.tm_year -= years;
    } else if (duration.find("M") != std::string::npos) {
        int months = std::stoi(duration.substr(0, duration.find("M")));
        tm.tm_mon -= months;
        while (tm.tm_mon < 0) {
            tm.tm_year -= 1;
            tm.tm_mon += 12;
        }
    }

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

std::string DailyDataFetcher::getCurrentDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    std::string current_date = oss.str();
    STX_LOGI(logger, "current date: " + current_date);
    return current_date;
}

// Helper functions: Calculate SMA, EMA, RSI, MACD, VWAP, Momentum
double DailyDataFetcher::calculateSMA(const std::string& symbol, double close, int period) {
    static std::map<std::string, std::deque<double>> closingPrices;
    closingPrices[symbol].push_back(close);

    if (closingPrices[symbol].size() > period) {
        closingPrices[symbol].pop_front();
    }

    if (closingPrices[symbol].size() < period) {
        return close;  // Return current price if not enough data
    }

    return std::accumulate(closingPrices[symbol].begin(), closingPrices[symbol].end(), 0.0) / period;
}

double DailyDataFetcher::calculateEMA(const std::string& symbol, double close, int period) {
    static std::map<std::string, double> emaValues;
    static std::map<std::string, int> dataPoints;

    double multiplier = 2.0 / (period + 1);

    if (emaValues.find(symbol) == emaValues.end() || dataPoints[symbol] == 0) {
        emaValues[symbol] = close;
        dataPoints[symbol] = 1;
    } else {
        emaValues[symbol] = (close - emaValues[symbol]) * multiplier + emaValues[symbol];
        dataPoints[symbol]++;
    }

    // Return SMA if we don't have enough data points for EMA
    if (dataPoints[symbol] < period) {
        return calculateSMA(symbol, close, period);
    }

    return emaValues[symbol];
}

double DailyDataFetcher::calculateRSI(const std::string& symbol, double close, int period) {
    static std::map<std::string, std::deque<double>> gains;
    static std::map<std::string, std::deque<double>> losses;
    static std::map<std::string, double> lastClose;

    if (lastClose.find(symbol) == lastClose.end()) {
        lastClose[symbol] = close;
        return 50.0;  // Default to neutral RSI if it's the first data point
    }

    double change = close - lastClose[symbol];
    lastClose[symbol] = close;

    gains[symbol].push_back(std::max(change, 0.0));
    losses[symbol].push_back(std::max(-change, 0.0));

    if (gains[symbol].size() > period) {
        gains[symbol].pop_front();
        losses[symbol].pop_front();
    }

    if (gains[symbol].size() < period) {
        return 50.0;  // Not enough data, return neutral RSI
    }

    double avgGain = std::accumulate(gains[symbol].begin(), gains[symbol].end(), 0.0) / period;
    double avgLoss = std::accumulate(losses[symbol].begin(), losses[symbol].end(), 0.0) / period;

    if (avgLoss == 0.0) return 100.0;

    double rs = avgGain / avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

double DailyDataFetcher::calculateMACD(const std::string& symbol, double close) {
    const int shortPeriod = 12;
    const int longPeriod = 26;

    double shortEMA = calculateEMA(symbol + "_short", close, shortPeriod);
    double longEMA = calculateEMA(symbol + "_long", close, longPeriod);

    return shortEMA - longEMA;
}

double DailyDataFetcher::calculateVWAP(const std::string& symbol, double volume, double close) {
    static std::map<std::string, double> cumulativePriceVolume;
    static std::map<std::string, double> cumulativeVolume;

    cumulativePriceVolume[symbol] += close * volume;
    cumulativeVolume[symbol] += volume;

    if (cumulativeVolume[symbol] == 0) {
        return close;  // Avoid division by zero
    }

    return cumulativePriceVolume[symbol] / cumulativeVolume[symbol];
}

double DailyDataFetcher::calculateMomentum(const std::string& symbol, double close, int period) {
    static std::map<std::string, std::deque<double>> priceHistory;

    priceHistory[symbol].push_back(close);

    if (priceHistory[symbol].size() > period) {
        priceHistory[symbol].pop_front();
    }

    if (priceHistory[symbol].size() < period) {
        return 0.0;  // Not enough data
    }

    return close - priceHistory[symbol].front();
}

void DailyDataFetcher::addToQueue(const std::string& date, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        dataQueue.emplace(date, historicalData);
        STX_LOGD(logger, date + " written into dataQueue, " + std::to_string(dataQueue.size()) + " items inside");
        if (dataQueue.size() > 5) {
            STX_LOGF(logger, "Size of dataQueue larger than 5, please check the status and procedure of writting data into database immediately!");
        }
    }
    queueCV.notify_one();
}

void DailyDataFetcher::writeToDatabaseFunc() {
    STX_LOGI(logger, "writeToDatabaseThread started.");
    while (running.load() || !dataQueue.empty()) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCV.wait(lock, [this] { return !dataQueue.empty() || !running.load(); });

        while (!dataQueue.empty()) {
            auto [date, data] = dataQueue.front();
            if (db->insertOrUpdateDailyData(date, data)) {
                dataQueue.pop();
                STX_LOGD(logger, "Written into database successfully, pop head item from data queue.");
            } else {
                STX_LOGE(logger, "Failed to write data to db: " + std::get<std::string>(data["symbol"]) + " " + date + ", will retry ...");
                break; // Exit the loop to retry later
            }
        }
    }
}