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
#include "DailyDataFetcher.h"

DailyDataFetcher::DailyDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& _db)
    : logger(logger), db(_db), 
      osSignal(std::make_unique<EReaderOSSignal>(2000)),
      client(nullptr),
      reader(nullptr), 
      connected(false), running(false), dataReceived(false), nextRequestId(0) {
    
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
    stop();
}

bool DailyDataFetcher::connectToIB() {
    std::lock_guard<std::mutex> lock(clientMutex);

    try {
        client = std::make_unique<EClientSocket>(this, osSignal.get());
        if (!client) {
            throw std::runtime_error("Failed to create EClientSocket");
        }

        const char *host = "127.0.0.1";
        int port = 7496;
        int clientId = 2;

        if (client->eConnect(host, port, clientId)) {
            STX_LOGI(logger, "Connected to IB TWS.");

            reader = std::make_unique<EReader>(client.get(), osSignal.get());
            reader->start();

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

void DailyDataFetcher::stop() {
    if (!running) return;

    running = false;
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        if (client && client->isConnected()) {
            client->eDisconnect();
            STX_LOGI(logger, "Disconnected from IB TWS.");
        }
        client.reset();
        reader.reset();
    }

    connected = false;
    STX_LOGI(logger, "DailyDataFetcher stopped and cleaned up.");
}

void DailyDataFetcher::fetchAndProcessDailyData(const std::string& symbol, const std::string& duration, bool incremental) {
    STX_LOGI(logger, "Fetching and processing historical data for symbol: " + symbol);

    if (!connectToIB()) {
        STX_LOGE(logger, "Failed to connect to IB TWS.");
        return;
    }

    running = true;

    std::string endDateTime = getCurrentDate();
    std::string startDateTime = incremental ? db->getLastDailyEndDate(symbol) : calculateStartDateFromDuration(duration);
    if (startDateTime.empty()) {
        startDateTime = calculateStartDateFromDuration(duration);
    }

    auto dateRanges = splitDateRange(startDateTime, endDateTime);

    for (const auto& range : dateRanges) {
        requestDailyData(symbol, range.first, range.second, "1 day");
        auto historicalData = historicalDataBuffer;

        for (const auto& data : historicalData) {
            storeDailyData(symbol, data);
        }
    }

    stop();
    STX_LOGI(logger, "Completed fetching and processing historical data for symbol: " + symbol);
}

void DailyDataFetcher::requestDailyData(const std::string& symbol, const std::string& startDate, const std::string& endDate, const std::string& barSize) {
    historicalDataBuffer.clear();
    STX_LOGI(logger, "Requesting historical data for " + symbol);

    Contract contract;
    contract.symbol = symbol;
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";

    std::string whatToShow = "TRADES";
    bool useRTH = true;
    int formatDate = 1;  // Use yyyymmdd hh:mm:ss format

    client->reqHistoricalData(nextRequestId++, contract, endDate, calculateStartDateFromDuration(barSize), barSize, whatToShow, useRTH, formatDate, false, TagValueListSPtr());
    waitForData();
    STX_LOGI(logger, "Completed historical data request for " + symbol);
}

void DailyDataFetcher::historicalData(TickerId reqId, const Bar& bar) {
    std::map<std::string, std::variant<double, std::string>> data;
    std::tm timeStruct{};
    parseDateString(bar.time, timeStruct);
    std::ostringstream oss;
    oss << std::put_time(&timeStruct, "%Y-%m-%d %H:%M:%S");
    data["date"] = oss.str();
    data["open"] = bar.open;
    data["high"] = bar.high;
    data["low"] = bar.low;
    data["close"] = bar.close;
    data["volume"] = bar.volume;

    historicalDataBuffer.push_back(data);
}

void DailyDataFetcher::historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) {
    std::unique_lock<std::mutex> lock(cvMutex);
    dataReceived = true;
    cv.notify_one();
}

void DailyDataFetcher::waitForData() {
    std::unique_lock<std::mutex> lock(cvMutex);
    cv.wait(lock, [this] { return dataReceived; });
    dataReceived = false;
}

void DailyDataFetcher::parseDateString(const std::string& dateStr, std::tm& timeStruct) {
    std::istringstream ss(dateStr);
    ss >> std::get_time(&timeStruct, "%Y%m%d %H:%M:%S");
}

void DailyDataFetcher::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    STX_LOGE(logger, "Error: " + std::to_string(id) + " - " + std::to_string(errorCode) + " - " + errorString);

    if (errorCode == 509 || errorCode == 1100) { 
        std::lock_guard<std::mutex> lock(cvMutex);
        connected = false;
        dataReceived = true;
        cv.notify_one();
    } else {
        std::unique_lock<std::mutex> lock(cvMutex);
        dataReceived = true;
        cv.notify_one();
    }
}

void DailyDataFetcher::nextValidId(OrderId orderId) {
    nextRequestId = orderId;
}

void DailyDataFetcher::storeDailyData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    std::string date = std::get<std::string>(historicalData.at("date"));
    
    double open = std::get<double>(historicalData.at("open"));
    double high = std::get<double>(historicalData.at("high"));
    double low = std::get<double>(historicalData.at("low"));
    double close = std::get<double>(historicalData.at("close"));
    double volume = std::get<double>(historicalData.at("volume"));

    // 计算特征指标
    double sma = calculateSMA(symbol, close);
    double ema = calculateEMA(symbol, close);
    double rsi = calculateRSI(symbol, close);
    double macd = calculateMACD(symbol, close);
    double vwap = calculateVWAP(symbol, volume, close);
    double momentum = calculateMomentum(symbol, close);

    // 存储数据到数据库
    if (db->insertDailyData(date, {
            {"symbol", symbol},
            {"open", open},
            {"high", high},
            {"low", low},
            {"close", close},
            {"volume", volume},
            {"sma", sma},
            {"ema", ema},
            {"rsi", rsi},
            {"macd", macd},
            {"vwap", vwap},
            {"momentum", momentum}
        })) {
        STX_LOGI(logger, "Daily data written to db successfully: " + symbol + " " + date);
    } else {
        STX_LOGE(logger, "Failed to write historical data to db: " + symbol + " " + date);
    }
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
        nextTm.tm_mday += 1; // 每次增加一天
        if (std::mktime(&nextTm) > std::mktime(&endTm)) {
            nextTm = endTm;
        }

        std::ostringstream ossStart;
        ossStart << std::put_time(&startTm, "%Y-%m-%d %H:%M:%S");

        std::ostringstream ossEnd;
        ossEnd << std::put_time(&nextTm, "%Y-%m-%d %H:%M:%S");

        dateRanges.emplace_back(ossStart.str(), ossEnd.str());

        startTm = nextTm;
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
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string DailyDataFetcher::getCurrentDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 辅助函数：计算 SMA、EMA、RSI、MACD、VWAP、Momentum
double DailyDataFetcher::calculateSMA(const std::string& symbol, double close) {
    static std::map<std::string, std::vector<double>> closingPrices;
    closingPrices[symbol].push_back(close);

    const int period = 20;
    if (closingPrices[symbol].size() < period) return 0.0;

    double sum = std::accumulate(closingPrices[symbol].end() - period, closingPrices[symbol].end(), 0.0);
    return sum / period;
}

double DailyDataFetcher::calculateEMA(const std::string& symbol, double close) {
    static std::map<std::string, double> emaValues;
    const int period = 20;
    double multiplier = 2.0 / (period + 1);

    if (emaValues.find(symbol) == emaValues.end()) {
        emaValues[symbol] = close;
    } else {
        emaValues[symbol] = ((close - emaValues[symbol]) * multiplier) + emaValues[symbol];
    }

    return emaValues[symbol];
}

double DailyDataFetcher::calculateRSI(const std::string& symbol, double close) {
    static std::map<std::string, std::vector<double>> gains;
    static std::map<std::string, std::vector<double>> losses;

    static double lastClose = close;
    double change = close - lastClose;
    lastClose = close;

    if (change > 0) {
        gains[symbol].push_back(change);
        losses[symbol].push_back(0.0);
    } else {
        losses[symbol].push_back(-change);
        gains[symbol].push_back(0.0);
    }

    const int period = 14;
    if (gains[symbol].size() < period) return 50.0;

    double avgGain = std::accumulate(gains[symbol].end() - period, gains[symbol].end(), 0.0) / period;
    double avgLoss = std::accumulate(losses[symbol].end() - period, losses[symbol].end(), 0.0) / period;

    double rs = avgGain / avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

double DailyDataFetcher::calculateMACD(const std::string& symbol, double close) {
    static std::map<std::string, double> shortEMA;
    static std::map<std::string, double> longEMA;

    const int shortPeriod = 12;
    const int longPeriod = 26;
    double shortMultiplier = 2.0 / (shortPeriod + 1);
    double longMultiplier = 2.0 / (longPeriod + 1);

    if (shortEMA.find(symbol) == shortEMA.end()) {
        shortEMA[symbol] = close;
    } else {
        shortEMA[symbol] = ((close - shortEMA[symbol]) * shortMultiplier) + shortEMA[symbol];
    }

    if (longEMA.find(symbol) == longEMA.end()) {
        longEMA[symbol] = close;
    } else {
        longEMA[symbol] = ((close - longEMA[symbol]) * longMultiplier) + longEMA[symbol];
    }

    return shortEMA[symbol] - longEMA[symbol];
}

double DailyDataFetcher::calculateVWAP(const std::string& symbol, double volume, double close) {
    static std::map<std::string, double> cumulativePriceVolume;
    static std::map<std::string, double> cumulativeVolume;

    cumulativePriceVolume[symbol] += close * volume;
    cumulativeVolume[symbol] += volume;

    return cumulativePriceVolume[symbol] / cumulativeVolume[symbol];
}

double DailyDataFetcher::calculateMomentum(const std::string& symbol, double close) {
    static std::map<std::string, std::deque<double>> priceHistory;

    const int period = 10;
    priceHistory[symbol].push_back(close);

    if (priceHistory[symbol].size() > period) {
        priceHistory[symbol].pop_front();
    }

    if (priceHistory[symbol].size() == period) {
        return close - priceHistory[symbol].front();
    } else {
        return 0.0;
    }
}