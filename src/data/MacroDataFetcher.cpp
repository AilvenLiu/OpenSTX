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

#include "MacroDataFetcher.h"
#include "ExternalAPIClient.h"  // Assuming we have a wrapper for external API

MacroDataFetcher::MacroDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& db)
    : logger(logger), db(db) {}

void MacroDataFetcher::fetchMacroData(const std::string& indicator) {
    STX_LOGI(logger, "Fetching macroeconomic data for indicator: " + indicator);

    ExternalAPIClient apiClient;
    auto macroData = apiClient.requestMacroData(indicator);

    for (const auto& data : macroData) {
        storeMacroData(data.at("date"), indicator, data.at("value"));
    }

    STX_LOGI(logger, "Completed fetching macroeconomic data for indicator: " + indicator);
}

void MacroDataFetcher::fetchEarningsData(const std::string& symbol) {
    STX_LOGI(logger, "Fetching earnings data for symbol: " + symbol);

    ExternalAPIClient apiClient;
    auto earningsData = apiClient.requestEarningsData(symbol);

    for (const auto& data : earningsData) {
        storeEarningsData(data.at("date"), symbol, data.at("earnings"));
    }

    STX_LOGI(logger, "Completed fetching earnings data for symbol: " + symbol);
}

void MacroDataFetcher::storeMacroData(const std::string& date, const std::string& indicator, double value) {
    db->insertMacroData(date, indicator, value);
}

void MacroDataFetcher::storeEarningsData(const std::string& date, const std::string& symbol, double earnings) {
    db->insertMacroData(date, "Earnings_" + symbol, earnings);
}