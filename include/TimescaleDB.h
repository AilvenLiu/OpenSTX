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

#ifndef TIMESCALEDB_H
#define TIMESCALEDB_H

#include <pqxx/pqxx>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include "Logger.h"

class TimescaleDB {
public:
    TimescaleDB(const std::shared_ptr<Logger>& logger, const std::string &dbname, const std::string &user, const std::string &password, const std::string &host, const std::string &port);
    ~TimescaleDB();

    void insertL1Data(const std::string &datetime, const std::map<std::string, double> &l1Data);
    void insertL2Data(const std::string &datetime, const std::vector<std::map<std::string, double>> &l2Data);
    void insertFeatureData(const std::string &datetime, const std::map<std::string, double> &features);

private:
    void createTables();
    void createDatabase(const std::string &dbname, const std::string &user, const std::string &password, const std::string &host, const std::string &port);
    void cleanupAndExit();
    
    std::shared_ptr<Logger> logger;
    pqxx::connection *conn;
};

#endif // TIMESCALEDB_H