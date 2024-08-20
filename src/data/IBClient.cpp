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

#include "IBClient.h"
#include <sstream>
#include <iomanip>

IBClient::IBClient(const std::shared_ptr<Logger>& log, const std::shared_ptr<TimescaleDB>& _db)
    : osSignal(std::make_unique<EReaderOSSignal>(2000)),
      client(std::make_unique<EClientSocket>(this, osSignal.get())),
      logger(log), db(_db), dataReceived(false), nextRequestId(0), connected(false) {}

IBClient::~IBClient() {
    disconnect();
}

bool IBClient::connect(const std::string& host, int port, int clientId) {
    try {
        if (connected) {
            STX_LOGW(logger, "Already connected. Skipping connect.");
            return true;
        }

        if (client->eConnect(host.c_str(), port, clientId)) {
            connected = true;
            STX_LOGI(logger, "Connected to IB TWS with clientId: " + std::to_string(clientId));
            return true;
        } else {
            STX_LOGE(logger, "Failed to connect to IB TWS with clientId: " + std::to_string(clientId));
            return false;
        }
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Exception during connect: " + std::string(e.what()));
        return false;
    }
}


void IBClient::disconnect() {
    try {
        if (connected && connected) {
            client->eDisconnect();
            connected = false;
            STX_LOGI(logger, "Disconnected from IB TWS");
        }
        client.reset();
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Exception during disconnect: " + std::string(e.what()));
    }
}

std::vector<std::map<std::string, std::variant<double, std::string>>> IBClient::requestHistoricalData(const std::string& symbol, const std::string& duration, const std::string& barSize, bool incremental) {
    historicalDataBuffer.clear();
    STX_LOGI(logger, "Requesting historical data for " + symbol);

    // 获取最后一次的结束日期（增量获取的情况下）
    std::string endDateTime = "";
    if (incremental) {
        endDateTime = db->getLastHistoricalEndDate(symbol);
    }

    Contract contract;
    contract.symbol = symbol;
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";

    std::string whatToShow = "TRADES";
    bool useRTH = true;
    int formatDate = 1;  // 使用 yyyymmdd hh:mm:ss 格式

    client->reqHistoricalData(nextRequestId++, contract, endDateTime, duration, barSize, whatToShow, useRTH, formatDate, false, TagValueListSPtr());

    waitForData();

    STX_LOGI(logger, "Received historical data for " + symbol);
    return historicalDataBuffer;
}

std::vector<std::map<std::string, std::variant<double, std::string>>> IBClient::requestOptionsData(const std::string& symbol) {
    optionsDataBuffer.clear();
    optionExpiryDates.clear();
    STX_LOGI(logger, "Requesting options data for " + symbol);

    auto expiryDates = getNextThreeExpiryDates(symbol);
    for (const auto& expiryDate : expiryDates) {
        Contract contract;
        contract.symbol = symbol;
        contract.secType = "OPT";
        contract.exchange = "SMART";
        contract.currency = "USD";
        contract.lastTradeDateOrContractMonth = expiryDate;

        client->reqContractDetails(nextRequestId++, contract);
    }

    waitForData();
    STX_LOGI(logger, "Received options data for " + symbol);
    return optionsDataBuffer;
}

std::vector<std::string> IBClient::getNextThreeExpiryDates(const std::string& symbol) {
    optionExpiryDates.clear();

    Contract contract;
    contract.symbol = symbol;
    contract.secType = "OPT";
    contract.exchange = "SMART";
    contract.currency = "USD";

    client->reqContractDetails(nextRequestId++, contract);
    waitForData();

    return optionExpiryDates;
}

void IBClient::historicalData(TickerId reqId, const Bar& bar) {
    std::map<std::string, std::variant<double, std::string>> data;
    std::tm timeStruct{};
    parseDateString(bar.time, timeStruct);
    std::ostringstream oss;
    oss << std::put_time(&timeStruct, "%Y-%m-%d %H:%M:%S");
    data["date"] = oss.str();
    data["open"] = bar.open;
    data["high"] = bar.high;
    data["low"] = bar.low;
    data["close"] = bar.close;
    data["volume"] = static_cast<double>(bar.volume);

    historicalDataBuffer.push_back(data);
}

void IBClient::contractDetails(int reqId, const ContractDetails& details) {
    std::string expiryDate = details.contract.lastTradeDateOrContractMonth;
    optionExpiryDates.push_back(expiryDate);

    if (optionExpiryDates.size() >= 3) {
        dataReceived = true;
        cv.notify_one();
    }
}

void IBClient::contractDetailsEnd(int reqId) {
    std::unique_lock<std::mutex> lock(mtx);
    dataReceived = true;
    cv.notify_one();
    STX_LOGI(logger, "Completed receiving contract details for request ID: " + std::to_string(reqId));
}

void IBClient::historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) {
    std::unique_lock<std::mutex> lock(mtx);
    dataReceived = true;
    cv.notify_one();
}

void IBClient::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    STX_LOGE(logger, "Error: " + std::to_string(id) + " - " + std::to_string(errorCode) + " - " + errorString);
    std::unique_lock<std::mutex> lock(mtx);
    dataReceived = true;  // 解锁等待
    cv.notify_one();
}

void IBClient::nextValidId(OrderId orderId) {
    nextRequestId = orderId;
}

void IBClient::waitForData() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return dataReceived; });
    dataReceived = false;
}

void IBClient::parseDateString(const std::string& dateStr, std::tm& timeStruct) {
    // 解析时间字符串，例如：yyyymmdd hh:mm:ss
    std::istringstream ss(dateStr);
    ss >> std::get_time(&timeStruct, "%Y%m%d %H:%M:%S");
}