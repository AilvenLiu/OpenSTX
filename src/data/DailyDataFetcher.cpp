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

void DailyDataFetcher::fetchAndProcessDailyData(const std::string& symbol, const std::string& duration, bool incremental) {
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
        startDateTime = db->getLastDailyEndDate(symbol);
        if (startDateTime.empty()) {
            startDateTime = calculateStartDateFromDuration(duration);
        }
    } else {
        startDateTime = calculateStartDateFromDuration(duration);
    }

    auto dateRanges = splitDateRange(startDateTime, endDateTime);

    for (const auto& range : dateRanges) {
        ibClient->requestDailyData(symbol, range.first, "1 day", incremental);
        auto historicalData = ibClient->getDailyData();

        for (const auto& data : historicalData) {
            storeDailyData(symbol, data);
        }
    }

    STX_LOGI(logger, "Completed fetching and processing historical data for symbol: " + symbol);
}

void DailyDataFetcher::storeDailyData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    std::string date = std::get<std::string>(historicalData.at("date"));
    if (db->insertDailyData(date, {
            {"symbol", symbol},
            {"open", std::get<double>(historicalData.at("open"))},
            {"high", std::get<double>(historicalData.at("high"))},
            {"low", std::get<double>(historicalData.at("low"))},
            {"close", std::get<double>(historicalData.at("close"))},
            {"volume", std::get<double>(historicalData.at("volume"))}})
    ) {
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