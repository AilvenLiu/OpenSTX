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

void HistoricalDataFetcher::fetchHistoricalData(const std::string& symbol, const std::string& duration, const std::string& barSize, bool incremental) {
    STX_LOGI(logger, "Fetching historical data for symbol: " + symbol);

    if (!ibClient->isConnected()) {
        STX_LOGW(logger, "Connection is not established. Attempting to reconnect...");
        if (!ibClient->connect("127.0.0.1", 7496, 2)) {
            STX_LOGE(logger, "Failed to connect IBClient for historical data fetching");
            return;
        }
    }

    auto historicalData = ibClient->requestHistoricalData(symbol, duration, barSize, incremental);

    for (const auto& data : historicalData) {
        storeHistoricalData(symbol, data);
    }

    STX_LOGI(logger, "Completed fetching historical data for symbol: " + symbol);
}

void HistoricalDataFetcher::fetchOptionsData(const std::string& symbol) {
    STX_LOGI(logger, "Fetching options data for symbol: " + symbol);

    auto optionsData = ibClient->requestOptionsData(symbol);

    if (!ibClient->isConnected()) {
        STX_LOGW(logger, "Connection is not established. Attempting to reconnect...");
        if (!ibClient->connect("127.0.0.1", 7496, 2)) { 
            STX_LOGE(logger, "Failed to connect IBClient for options data fetching");
            return;
        }
    }

    for (const auto& data : optionsData) {
        storeOptionsData(symbol, data);
    }

    STX_LOGI(logger, "Completed fetching options data for symbol: " + symbol);
}

void HistoricalDataFetcher::storeHistoricalData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData) {
    std::string date = std::get<std::string>(historicalData.at("date"));
    db->insertHistoricalData(date, {
        {"symbol", symbol},
        {"open", std::get<double>(historicalData.at("open"))},
        {"high", std::get<double>(historicalData.at("high"))},
        {"low", std::get<double>(historicalData.at("low"))},
        {"close", std::get<double>(historicalData.at("close"))},
        {"volume", std::get<double>(historicalData.at("volume"))}
    });
}

void HistoricalDataFetcher::storeOptionsData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& optionsData) {
    std::string date = std::get<std::string>(optionsData.at("date"));
    db->insertOptionsData(date, {
        {"symbol", symbol},
        {"option_type", std::get<std::string>(optionsData.at("option_type"))},
        {"strike_price", std::get<double>(optionsData.at("strike_price"))},
        {"expiration_date", std::get<std::string>(optionsData.at("expiration_date"))},
        {"implied_volatility", std::get<double>(optionsData.at("implied_volatility"))},
        {"delta", std::get<double>(optionsData.at("delta"))},
        {"gamma", std::get<double>(optionsData.at("gamma"))},
        {"theta", std::get<double>(optionsData.at("theta"))},
        {"vega", std::get<double>(optionsData.at("vega"))}
    });
}