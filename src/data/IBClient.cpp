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
#include <thread>

IBClient::IBClient(const std::shared_ptr<Logger>& log, const std::shared_ptr<TimescaleDB>& _db)
    : osSignal(std::make_unique<EReaderOSSignal>(2000)),
      client(std::make_unique<EClientSocket>(this, osSignal.get())),
      reader(std::make_unique<EReader>(client.get(), osSignal.get())),
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

            // 启动 EReader 线程以处理服务器消息
            if (!reader) {
                reader = std::make_unique<EReader>(client.get(), osSignal.get());
            }
            reader->start();
            std::thread([this]() {
                while (client->isConnected()) {
                    osSignal->waitForSignal();
                    std::lock_guard<std::mutex> lock(mtx);
                    reader->processMsgs();
                }
            }).detach();

            return true;
        } else {
            STX_LOGE(logger, "Failed to connect to IB TWS with clientId: " + std::to_string(clientId));
            connected = false; // 确保连接失败时状态正确
            return false;
        }
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Exception during connect: " + std::string(e.what()));
        connected = false; // 确保异常时状态正确
        return false;
    }
}

void IBClient::disconnect() {
    try {
        if (connected) {
            client->eDisconnect();
            connected = false;
            STX_LOGI(logger, "Disconnected from IB TWS");
        }
    } catch (const std::exception& e) {
        STX_LOGE(logger, "Exception during disconnect: " + std::string(e.what()));
    }
}

void IBClient::requestDailyData(const std::string& symbol, const std::string& duration, const std::string& barSize, bool incremental) {
    historicalDataBuffer.clear();
    STX_LOGI(logger, "Requesting historical data for " + symbol);

    std::string endDateTime = "";
    if (incremental) {
        endDateTime = db->getLastDailyEndDate(symbol);
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
    STX_LOGI(logger, "Completed historical data request for " + symbol);
}

std::vector<std::map<std::string, std::variant<double, std::string>>> IBClient::getDailyData() {
    return historicalDataBuffer;
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

void IBClient::historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) {
    std::unique_lock<std::mutex> lock(mtx);
    dataReceived = true;
    cv.notify_one();
}

void IBClient::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    STX_LOGE(logger, "Error: " + std::to_string(id) + " - " + std::to_string(errorCode) + " - " + errorString);

    if (errorCode == 509 || errorCode == 1100) {  // 处理特定错误码，例如连接断开或重置
        std::lock_guard<std::mutex> lock(mtx);
        connected = false;  // 确保在发生错误时正确更新连接状态
        dataReceived = true;  // 解锁等待中的线程
        cv.notify_one();
    } else {
        std::unique_lock<std::mutex> lock(mtx);
        dataReceived = true;  // 对其他错误解锁等待
        cv.notify_one();
    }
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
    std::istringstream ss(dateStr);
    ss >> std::get_time(&timeStruct, "%Y%m%d %H:%M:%S");
}