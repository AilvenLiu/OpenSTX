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

#ifndef HISTORICALDATAFERCHER_H
#define HISTORICALDATAFERCHER_H

#include <memory>
#include <variant>
#include "IBClient.h"
#include "TimescaleDB.h"
#include "Logger.h"

class HistoricalDataFetcher {
public:
    HistoricalDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& db);
    void fetchHistoricalData(const std::string& symbol, const std::string& duration, const std::string& barSize);
    void fetchOptionsData(const std::string& symbol, const std::string& expirationDate);

private:
    void storeHistoricalData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& historicalData);
    void storeOptionsData(const std::string& symbol, const std::map<std::string, std::variant<double, std::string>>& optionsData);

    std::shared_ptr<Logger> logger;
    std::shared_ptr<TimescaleDB> db;
    std::unique_ptr<IBClient> ibClient;
};

#endif