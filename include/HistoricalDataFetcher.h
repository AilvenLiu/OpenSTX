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

#ifndef DAILY_DATA_FERCHER_H
#define DAILY_DATA_FERCHER_H

#include <memory>
#include <variant>
#include <string>
#include <vector>
#include <map>
#include "IBClient.h"
#include "TimescaleDB.h"
#include "Logger.h"

class DailyDataFetcher {
public:
    DailyDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& _db);
    ~DailyDataFetcher();
    void fetchAndProcessHistoricalData(const std::string& symbol, const std::string& duration, bool incremental);
    void stop();
    inline const bool isConnected() const {
        return ibClient->isConnected();
    }

private:
    std::vector<std::pair<std::string, std::string>> splitDateRange(const std::string& startDate, const std::string& endDate);
    std::string calculateStartDateFromDuration(const std::string& duration);
    std::string getCurrentDate();
    void storeHistoricalData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData);
    void calculateAndStoreOptionsData(const std::string& date, const std::map<std::string, std::variant<double, std::string>>& historicalData);

    std::shared_ptr<Logger> logger;
    std::shared_ptr<TimescaleDB> db;
    std::unique_ptr<IBClient> ibClient;

    double calculateImpliedVolatility(const std::map<std::string, std::variant<double, std::string>>& historicalData);
    double calculateDelta(double impliedVolatility, double spotPrice, double strikePrice, double timeToExpiration, double riskFreeRate, double volatility);
    double calculateGamma(double delta, double spotPrice, double volatility, double timeToExpiration);
    double calculateTheta(double impliedVolatility, double spotPrice, double strikePrice, double timeToExpiration, double riskFreeRate, double volatility);
    double calculateVega(double impliedVolatility, double spotPrice, double timeToExpiration);
};


#endif