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

#ifndef MACRODATAFETCHER_H
#define MACRODATAFETCHER_H

#include <iostream>
#include <string>
#include <memory>
#include "TimescaleDB.h"
#include "Logger.h"

class MacroDataFetcher {
public:
    MacroDataFetcher(const std::shared_ptr<Logger>& logger, const std::shared_ptr<TimescaleDB>& db);
    void fetchMacroData(const std::string& indicator);
    void fetchEarningsData(const std::string& symbol);

private:
    void storeMacroData(const std::string& date, const std::string& indicator, double value);
    void storeEarningsData(const std::string& date, const std::string& symbol, double earnings);

    std::shared_ptr<Logger> logger;
    std::shared_ptr<TimescaleDB> db;
};

#endif