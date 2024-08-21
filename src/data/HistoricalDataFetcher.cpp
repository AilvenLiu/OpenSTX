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

#include "HistoricalDataFetcher.h"
#include <sstream>
#include <iomanip>

HistoricalDataFetcher::HistoricalDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& _db)
    : logger(logger), db(_db), ibClient(std::make_unique<IBClient>(logger, db)) {}

HistoricalDataFetcher::~HistoricalDataFetcher() {
    STX_LOGW(logger, "Destructor called, cleaning up resources.");
    stop();
}

void HistoricalDataFetcher::stop() {
    ibClient->disconnect();
    STX_LOGW(logger, "Resource released.");
}

void HistoricalDataFetcher::fetchHistoricalData(const std::string& symbol, const std::string& duration = "3 Y", const std::string& barSize = "1 day", bool incremental = true) {
    STX_LOGI(logger, "Fetching historical data for symbol: " + symbol);

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
        ibClient->requestHistoricalData(symbol, range.first, barSize, incremental);
        auto historicalData = ibClient->getHistoricalData();

        for (const auto& data : historicalData) {
            storeHistoricalData(symbol, data);
        }
    }

    STX_LOGI(logger, "Completed fetching historical data for symbol: " + symbol);
}

void HistoricalDataFetcher::fetchOptionsData(const std::string& symbol) {
    STX_LOGI(logger, "Fetching options data for symbol: " + symbol);

    if (!ibClient->isConnected()) {
        STX_LOGW(logger, "Connection is not established. Attempting to reconnect...");
        if (!ibClient->connect("127.0.0.1", 7496, 2)) { 
            STX_LOGE(logger, "Failed to connect IBClient for options data fetching");
            return;
        }
    }

    ibClient->requestOptionsData(symbol);
    auto optionsData = ibClient->getOptionsData();

    for (const auto& data : optionsData) {
        storeOptionsData(symbol, data);
    }

    STX_LOGI(logger, "Completed fetching options data for symbol: " + symbol);
}

void HistoricalDataFetcher::storeHistoricalData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    std::string date = std::get<std::string>(historicalData.at("date"));
    if (db->insertHistoricalData(date, {
            {"symbol", symbol},
            {"open", std::get<double>(historicalData.at("open"))},
            {"high", std::get<double>(historicalData.at("high"))},
            {"low", std::get<double>(historicalData.at("low"))},
            {"close", std::get<double>(historicalData.at("close"))},
            {"volume", std::get<double>(historicalData.at("volume"))}})
    ) {
        STX_LOGI(logger, std::string("Historical data written to db success: ") + symbol + " " + date );
    } else {
        STX_LOGE(logger, std::string("Historical data written to db failed: ") + symbol + " " + date );
    }
}

void HistoricalDataFetcher::storeOptionsData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& optionsData) {
    std::string date = std::get<std::string>(optionsData.at("date"));
    if (db->insertOptionsData(date, {
            {"symbol", symbol},
            {"option_type", std::get<std::string>(optionsData.at("option_type"))},
            {"strike_price", std::get<double>(optionsData.at("strike_price"))},
            {"expiration_date", std::get<std::string>(optionsData.at("expiration_date"))},
            {"implied_volatility", std::get<double>(optionsData.at("implied_volatility"))},
            {"delta", std::get<double>(optionsData.at("delta"))},
            {"gamma", std::get<double>(optionsData.at("gamma"))},
            {"theta", std::get<double>(optionsData.at("theta"))},
            {"vega", std::get<double>(optionsData.at("vega"))}})
    ) {
        STX_LOGI(logger, std::string("Option data written to db success: ") + symbol + " " + date );
    } else {
        STX_LOGE(logger, std::string("Option data written to db failed: ") + symbol + " " + date );
    }
}

std::vector<std::pair<std::string, std::string>> HistoricalDataFetcher::splitDateRange(const std::string& startDate, const std::string& endDate) {
    std::vector<std::pair<std::string, std::string>> dateRanges;
    std::tm startTm = {};
    std::tm endTm = {};

    std::istringstream ssStart(startDate);
    ssStart >> std::get_time(&startTm, "%Y-%m-%d");

    std::istringstream ssEnd(endDate);
    ssEnd >> std::get_time(&endTm, "%Y-%m-%d");

    while (std::difftime(std::mktime(&endTm), std::mktime(&startTm)) > 0) {
        std::tm nextTm = startTm;
        nextTm.tm_mon += 1; // 每次增加一个月
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

std::string HistoricalDataFetcher::calculateStartDateFromDuration(const std::string& duration) {
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

    // 确保时间格式为%Y-%m-%d %H:%M:%S
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string HistoricalDataFetcher::getCurrentDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}