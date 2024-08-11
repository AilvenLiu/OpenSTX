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

#include <numeric>
#include "RealTimeData.h"

// Constructor
RealTimeData::RealTimeData(const std::shared_ptr<Logger>& log, const std::shared_ptr<TimescaleDB>& db)
    : client(nullptr), logger(log), timescaleDB(db), nextOrderId(0), requestId(0), yesterdayClose(0.0) {

    // Initialize paths and open files for writing
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-d");
    std::string date_today = oss.str();

    l1FilePath = "data/realtime_original_data/l1_data_" + date_today + ".csv";
    l2FilePath = "data/realtime_original_data/l2_data_" + date_today + ".csv";
    combinedFilePath = "data/daily_realtime_data/combined_data_" + date_today + ".csv";

    l1DataFile.open(l1FilePath, std::ios::app);
    l2DataFile.open(l2FilePath, std::ios::app);
    combinedDataFile.open(combinedFilePath, std::ios::app);

    // Write headers to the files
    l1DataFile << "Datetime,Bid,Ask,Last,Open,High,Low,Close,Volume\n";
    l2DataFile << "Datetime,PriceLevel,BidPrice,BidSize,AskPrice,AskSize\n";
    combinedDataFile << "Datetime,Open,High,Low,Close,Volume,BidAskSpread,Midpoint,PriceChange,L2Depth,Gap\n";

    connectToIB();
}

// Destructor
RealTimeData::~RealTimeData() {
    if (l1DataFile.is_open()) l1DataFile.close();
    if (l2DataFile.is_open()) l2DataFile.close();
    if (combinedDataFile.is_open()) l1DataFile.close();
    if (client) client.reset();
    boost::interprocess::shared_memory_object::remove("RealTimeData");
}

void RealTimeData::connectToIB() {
    client = std::make_shared<EClientSocket>(this, nullptr);
    const char *host = "127.0.0.1";
    int port = 7496;
    int clientId = 0;

    if (client->eConnect(host, port, clientId)) {
        STX_LOGI(logger, "Connected to IB TWS");
    } else {
        STX_LOGE(logger, "Failed to connect to IB TWS");
    }
}

void RealTimeData::start() {
    // Clear any existing shared memory object with the same name
    boost::interprocess::shared_memory_object::remove("RealTimeData");
    
    // Initialize shared memory
    shm = boost::interprocess::shared_memory_object(boost::interprocess::create_only, "RealTimeData", boost::interprocess::read_write);
    shm.truncate(1024);  // Adjust size as needed
    region = boost::interprocess::mapped_region(shm, boost::interprocess::read_write);

    std::time_t lastMinute = std::time(nullptr) / 60;

    while (true) {
        if (isMarketOpen()) {
            requestData();

            std::time_t currentMinute = std::time(nullptr) / 60;
            if (currentMinute != lastMinute) {
                aggregateMinuteData();
                lastMinute = currentMinute;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));  // Adjust frequency as needed
        } else {
            std::this_thread::sleep_for(std::chrono::minutes(1));  // Check again after 1 minute
        }
    }
}

bool RealTimeData::isMarketOpen() {
    std::time_t nyTime = getNYTime();
    std::tm *tm = std::localtime(&nyTime);

    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    STX_LOGI(logger, "Current New York Time: " + std::string(buf));

    // Market open from 9:30 to 16:00
    return (tm->tm_hour > 9 && tm->tm_hour < 16) || (tm->tm_hour == 9 && tm->tm_min >= 30);
}

std::time_t RealTimeData::getNYTime() {
    std::time_t t = std::time(nullptr);
    std::tm *utc_tm = std::gmtime(&t);
    utc_tm->tm_hour -= 4;  // UTC-4 for Eastern Daylight Time (EDT)
    return std::mktime(utc_tm);
}

void RealTimeData::requestData() {
    Contract contract;
    contract.symbol = "SPY";
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";

    // Request L1 and L2 data
    client->reqMktData(++requestId, contract, "", false, false, TagValueListSPtr());
    client->reqMktDepth(++requestId, contract, 10, true, TagValueListSPtr()); // Request 10 levels of market depth
}

void RealTimeData::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib &attrib) {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (field == LAST) {
        l1Prices.push_back(price);
    }

    // Write to shared memory
    std::ostringstream oss;
    oss << "Tick Price: " << tickerId << " Field: " << field << " Price: " << price;
    STX_LOGI(logger, oss.str());
    writeToSharedMemory(oss.str());
}

void RealTimeData::tickSize(TickerId tickerId, TickType field, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);

    if (field == LAST_SIZE) {
        l1Volumes.push_back(size);
    }

    // Write to shared memory
    std::ostringstream oss;
    oss << "Tick Size: " << tickerId << " Field: " << field << " Size: " << size;
    STX_LOGI(logger, oss.str());
    writeToSharedMemory(oss.str());
}

void RealTimeData::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);
    processL2Data(position, price, size, side);

    std::ostringstream oss;
    oss << "Update Mkt Depth: " << id << " Position: " << position << " Operation: " << operation << " Side: " << side << " Price: " << price << " Size: " << size;
    STX_LOGI(logger, oss.str());
    writeToSharedMemory(oss.str());
}

void RealTimeData::processL2Data(int position, double price, Decimal size, int side) {
    std::map<std::string, double> data;
    data["Position"] = position;

    if (side == 1) {  // Bid side
        data["BidPrice"] = price;
        data["BidSize"] = size;
        data["AskPrice"] = 0.0; // Not available
        data["AskSize"] = 0.0; // Not available
    } else {  // Ask side
        data["BidPrice"] = 0.0; // Not available
        data["BidSize"] = 0.0; // Not available
        data["AskPrice"] = price;
        data["AskSize"] = size;
    }

    l2Data.push_back(data);

    // Write L2 data to file immediately
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream time_oss;
    time_oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    std::string datetime = time_oss.str();

    std::ostringstream oss;
    oss << datetime << "," << position << "," << data["BidPrice"] << "," << data["BidSize"] << ","
        << data["AskPrice"] << "," << data["AskSize"] << "\n";

    if (l2DataFile.is_open()) {
        l2DataFile << oss.str();
    }
}

void RealTimeData::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    std::lock_guard<std::mutex> lock(dataMutex);

    std::ostringstream oss;
    oss << "Error ID: " << id << " Code: " << errorCode << " Msg: " << errorString;
    STX_LOGE(logger, oss.str());
}

void RealTimeData::nextValidId(OrderId orderId) {
    nextOrderId = orderId;
    STX_LOGI(logger, "Next valid order ID: " + std::to_string(orderId));
}

void RealTimeData::aggregateMinuteData() {
    if (l1Prices.empty()) {
        return;  // No data to aggregate
    }

    std::lock_guard<std::mutex> lock(dataMutex);

    double open = l1Prices.front();
    double close = l1Prices.back();
    double high = *std::max_element(l1Prices.begin(), l1Prices.end());
    double low = *std::min_element(l1Prices.begin(), l1Prices.end());
    double volume = std::accumulate(l1Volumes.begin(), l1Volumes.end(), 0.0);

    double bidAskSpread = 0.0, midpoint = 0.0, priceChange = 0.0;
    double totalL2Volume = 0.0;

    for (const auto &data : l2Data) {
        totalL2Volume += data.at("BidSize") + data.at("AskSize");
    }

    if (!l2Data.empty()) {
        bidAskSpread = l2Data.back().at("AskPrice") - l2Data.front().at("BidPrice");
        midpoint = (l2Data.front().at("BidPrice") + l2Data.back().at("AskPrice")) / 2.0;
    }

    double gap = open - yesterdayClose;
    yesterdayClose = close;  // Update for the next day

    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    std::string datetime = oss.str();

    // Write aggregated data to combinedDataFile
    std::stringstream combinedData;
    combinedData << datetime << "," << open << "," << high << "," << low << "," << close << "," << volume << ","
                 << bidAskSpread << "," << midpoint << "," << priceChange << "," << totalL2Volume << "," << gap << "\n";

    if (combinedDataFile.is_open()) {
        combinedDataFile << combinedData.str();
    }

    // Insert into TimescaleDB
    timescaleDB->insertL1Data(datetime, {{"Bid", l1Prices.front()}, {"Ask", l1Prices.back()}, {"Last", close}, {"Open", open}, {"High", high}, {"Low", low}, {"Close", close}, {"Volume", volume}});
    timescaleDB->insertL2Data(datetime, l2Data);
    timescaleDB->insertFeatureData(datetime, {{"Gap", gap}, {"TodayOpen", open}, {"TotalL2Volume", totalL2Volume}});

    // Write to shared memory
    writeToSharedMemory(combinedData.str());

    // Clear temporary data
    l1Prices.clear();
    l1Volumes.clear();
    l2Data.clear();
}

void RealTimeData::writeToSharedMemory(const std::string &data) {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::memcpy(region.get_address(), data.c_str(), data.size());
}