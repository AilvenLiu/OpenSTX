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

#include "DailyDataFetcher.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <stdexcept>

DailyDataFetcher::DailyDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& _db)
    : logger(logger), db(_db), ibClient(std::make_unique<IBClient>(logger, db)) {}

DailyDataFetcher::~DailyDataFetcher() {
    STX_LOGW(logger, "Destructor called, cleaning up resources.");
    stop();
}

void DailyDataFetcher::stop() {
    ibClient->disconnect();
    STX_LOGW(logger, "Resources released.");
}

void DailyDataFetcher::fetchAndProcessHistoricalData(const std::string& symbol, const std::string& duration, bool incremental) {
    STX_LOGI(logger, "Fetching and processing historical data for symbol: " + symbol);

    if (!ibClient->isConnected()) {
        STX_LOGW(logger, "Connection is not established. Attempting to reconnect...");
        if (!ibClient->connect("127.0.0.1", 7496, 2)) {
            STX_LOGE(logger, "Failed to connect IBClient for historical data fetching");
            return;
        }
    }

    std::string endDateTime = getCurrentDate();
    std::string startDateTime;

    if (incremental) {
        startDateTime = db->getLastHistoricalEndDate(symbol);
        if (startDateTime.empty()) {
            startDateTime = calculateStartDateFromDuration(duration);
        }
    } else {
        startDateTime = calculateStartDateFromDuration(duration);
    }

    auto dateRanges = splitDateRange(startDateTime, endDateTime);

    for (const auto& range : dateRanges) {
        ibClient->requestHistoricalData(symbol, range.first, "1 day", incremental);
        auto historicalData = ibClient->getHistoricalData();

        for (const auto& data : historicalData) {
            storeHistoricalData(symbol, data);
            calculateAndStoreOptionsData(std::get<std::string>(data.at("date")), data);
        }
    }

    STX_LOGI(logger, "Completed fetching and processing historical data for symbol: " + symbol);
}

void DailyDataFetcher::storeHistoricalData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    std::string date = std::get<std::string>(historicalData.at("date"));
    if (db->insertHistoricalData(date, {
            {"symbol", symbol},
            {"open", std::get<double>(historicalData.at("open"))},
            {"high", std::get<double>(historicalData.at("high"))},
            {"low", std::get<double>(historicalData.at("low"))},
            {"close", std::get<double>(historicalData.at("close"))},
            {"volume", std::get<double>(historicalData.at("volume"))}})
    ) {
        STX_LOGI(logger, "Historical data written to db successfully: " + symbol + " " + date);
    } else {
        STX_LOGE(logger, "Failed to write historical data to db: " + symbol + " " + date);
    }
}

void DailyDataFetcher::calculateAndStoreOptionsData(const std::string& date, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    STX_LOGI(logger, "Calculating and storing options data for date: " + date);

    // 获取股票价格和其他相关数据
    double spotPrice = std::get<double>(historicalData.at("close"));
    double strikePrice = spotPrice * 1.05;  // 计算期权时需要使用的行权价
    double timeToExpiration = 30.0 / 365.0; // 假设期权到期时间为30天
    double riskFreeRate = 0.01; // 无风险利率

    // 计算隐含波动率、Delta、Gamma、Theta、Vega
    double impliedVolatility = calculateImpliedVolatility(historicalData);
    double delta = calculateDelta(spotPrice, strikePrice, timeToExpiration, riskFreeRate, impliedVolatility);
    double gamma = calculateGamma(delta, spotPrice, impliedVolatility, timeToExpiration);
    double theta = calculateTheta(spotPrice, strikePrice, timeToExpiration, riskFreeRate, impliedVolatility);
    double vega = calculateVega(spotPrice, timeToExpiration, impliedVolatility);

    // 插入计算出的期权数据到数据库
    std::map<std::string, std::variant<double, std::string>> optionsData = {
        {"symbol", "SPY"},
        {"implied_volatility", impliedVolatility},
        {"delta", delta},
        {"gamma", gamma},
        {"theta", theta},
        {"vega", vega}
    };

    if (db->insertDailyOptionsData(date, optionsData)) {
        STX_LOGI(logger, "Options data calculated and stored successfully for date: " + date);
    } else {
        STX_LOGE(logger, "Failed to store options data for date: " + date);
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

// 计算期权的隐含波动率
double DailyDataFetcher::calculateImpliedVolatility(const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    double closePrice = std::get<double>(historicalData.at("close"));
    double highPrice = std::get<double>(historicalData.at("high"));
    double lowPrice = std::get<double>(historicalData.at("low"));

    return (highPrice - lowPrice) / closePrice;
}

// 计算期权的 Delta
double DailyDataFetcher::calculateDelta(double spotPrice, double strikePrice, double timeToExpiration, double riskFreeRate, double volatility) {
    double d1 = (std::log(spotPrice / strikePrice) + (riskFreeRate + 0.5 * std::pow(volatility, 2)) * timeToExpiration) / (volatility * std::sqrt(timeToExpiration));
    return std::exp(-riskFreeRate * timeToExpiration) * std::exp(-0.5 * std::pow(d1, 2)) / (volatility * spotPrice * std::sqrt(2 * M_PI * timeToExpiration));
}

// 计算期权的 Gamma
double DailyDataFetcher::calculateGamma(double delta, double spotPrice, double volatility, double timeToExpiration) {
    return delta

 / (spotPrice * volatility * std::sqrt(timeToExpiration));
}

// 计算期权的 Theta
double DailyDataFetcher::calculateTheta(double spotPrice, double strikePrice, double timeToExpiration, double riskFreeRate, double volatility) {
    double d1 = (std::log(spotPrice / strikePrice) + (riskFreeRate + 0.5 * std::pow(volatility, 2)) * timeToExpiration) / (volatility * std::sqrt(timeToExpiration));
    double d2 = d1 - volatility * std::sqrt(timeToExpiration);
    return -spotPrice * std::exp(-riskFreeRate * timeToExpiration) * std::exp(-0.5 * std::pow(d1, 2)) * volatility / (2 * std::sqrt(2 * M_PI * timeToExpiration)) -
           riskFreeRate * strikePrice * std::exp(-riskFreeRate * timeToExpiration) * std::exp(-0.5 * std::pow(d2, 2)) / (volatility * std::sqrt(2 * M_PI * timeToExpiration));
}

// 计算期权的 Vega
double DailyDataFetcher::calculateVega(double spotPrice, double timeToExpiration, double volatility) {
    return spotPrice * std::sqrt(timeToExpiration) * std::exp(-0.5 * std::pow(volatility, 2)) / std::sqrt(2 * M_PI);
}