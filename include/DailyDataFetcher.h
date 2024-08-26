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
#include <deque>
#include "IBClient.h"
#include "TimescaleDB.h"
#include "Logger.h"

class DailyDataFetcher {
public:
    DailyDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& _db);
    ~DailyDataFetcher();
    void fetchAndProcessDailyData(const std::string& symbol, const std::string& duration, bool incremental);
    void stop();
    inline const bool isConnected() const {
        return ibClient->isConnected();
    }

private:
    std::vector<std::pair<std::string, std::string>> splitDateRange(const std::string& startDate, const std::string& endDate);
    std::string calculateStartDateFromDuration(const std::string& duration);
    std::string getCurrentDate();
    void storeDailyData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData);

    // 新增的技术指标计算函数声明
    double calculateSMA(const std::string& symbol, double close);
    double calculateEMA(const std::string& symbol, double close);
    double calculateRSI(const std::string& symbol, double close);
    double calculateMACD(const std::string& symbol, double close);
    double calculateVWAP(const std::string& symbol, double volume, double close);
    double calculateMomentum(const std::string& symbol, double close);

    // 成员变量
    std::shared_ptr<Logger> logger;
    std::shared_ptr<TimescaleDB> db;
    std::unique_ptr<IBClient> ibClient;
};

#endif