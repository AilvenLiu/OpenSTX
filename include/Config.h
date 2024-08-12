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

#ifndef CONFIG_H
#define CONFIG_H

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iostream>
#include <string>

#include "Logger.h"

struct DBConfig {
    std::string host;
    std::string port;
    std::string dbname;
    std::string user;
    std::string password;
};

DBConfig loadConfig(const std::string& configFilePath, const std::shared_ptr<Logger>& logger) {
    DBConfig config;
    boost::property_tree::ptree pt;

    try {
        boost::property_tree::ini_parser::read_ini(configFilePath, pt);

        // 根据 usecloud 选项选择加载的配置部分
        bool useCloud = pt.get<bool>("usecloud.usecloud", false);
        std::string section = useCloud ? "cloud" : "local";

        config.host = pt.get<std::string>(section + ".host");
        config.port = pt.get<std::string>(section + ".port");
        config.dbname = pt.get<std::string>(section + ".dbname");
        config.user = pt.get<std::string>(section + ".user");
        config.password = pt.get<std::string>(section + ".password");

        std::string success_info = std::string("Using ") + (useCloud ? "cloud" : "local") + " database configuration.";

        STX_LOGI(logger, success_info);
    } catch (const std::exception& e) {
        std::string failure_info = std::string("Error reading configuration file: ") + e.what();
        STX_LOGE(logger, failure_info);
        throw;
    }

    return config;
}
#endif