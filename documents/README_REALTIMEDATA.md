# Detailed Explanation of `RealTimeData.cpp` Source Code

## File Overview

This source file implements the `RealTimeData` class, which handles real-time data retrieval from Interactive Brokers (IB) TWS (Trader Workstation), processes the data, and stores it in both CSV files and shared memory. The class also aggregates data on a per-minute basis.

## Class `RealTimeData`

### Constructor

```cpp
RealTimeData::RealTimeData(const std::shared_ptr<Logger>& log) : client(nullptr), logger(log), nextOrderId(0), requestId(0), yesterdayClose(0.0) {
    // Initialize paths and open files for writing
    l1FilePath = "data/realtime_original_data/l1_data.csv";
    l2FilePath = "data/realtime_original_data/l2_data.csv";
    combinedFilePath = "data/daily_realtime_data/combined_data.csv";

    l1DataFile.open(l1FilePath, std::ios::app);
    l2DataFile.open(l2FilePath, std::ios::app);
    combinedDataFile.open(combinedFilePath, std::ios::app);

    connectToIB();
}
```

- Initializes file paths for L1, L2, and combined data CSV files.
- Opens these files in append mode.
- Calls `connectToIB()` to establish a connection to IB TWS.

### Destructor

```cpp
RealTimeData::~RealTimeData() {
    if (l1DataFile.is_open()) l1DataFile.close();
    if (l2DataFile.is_open()) l2DataFile.close();
    if (combinedDataFile.is_open()) l1DataFile.close();
    if (client) client.reset();
    boost::interprocess::shared_memory_object::remove("RealTimeData");
}
```

- Closes the data files if they are open.
- Resets the `client` smart pointer to clean up the `EClientSocket` instance.
- Removes the shared memory object.

### `connectToIB()`

```cpp
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
```

- Creates an `EClientSocket` instance and attempts to connect to IB TWS.
- Logs the connection status.

### `start()`

```cpp
void RealTimeData::start() {
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
```

- Initializes shared memory for inter-process communication.
- Continuously checks if the market is open.
- Requests data every second (or other specified interval) when the market is open.
- Aggregates data every minute.
- Sleeps for one second or one minute based on market status.

### `isMarketOpen()`

```cpp
bool RealTimeData::isMarketOpen() {
    std::time_t nyTime = getNYTime();
    std::tm *tm = std::localtime(&nyTime);

    // Market open from 9:30 to 16:00
    return (tm->tm_hour > 9 && tm->tm_hour < 16) || (tm->tm_hour == 9 && tm->tm_min >= 30);
}
```

- Determines if the market is open based on New York time (EDT).

### `getNYTime()`

```cpp
std::time_t RealTimeData::getNYTime() {
    std::time_t t = std::time(nullptr);
    std::tm *utc_tm = std::gmtime(&t);
    utc_tm->tm_hour -= 4;  // UTC-4 for Eastern Daylight Time (EDT)
    return std::mktime(utc_tm);
}
```

- Converts the current UTC time to New York time by subtracting 4 hours.

### `requestData()`

```cpp
void RealTimeData::requestData() {
    Contract contract;
    contract.symbol = "SPY";
    contract.secType = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";

    // Request L1 and L2 data
    client->reqMktData(++requestId, contract, "", false, false, TagValueListSPtr());
    client->reqMktDepth(++requestId, contract, 5, true, TagValueListSPtr());
}
```

- Configures the contract details for "SPY" stock.
- Requests market data (L1) and market depth data (L2) from IB TWS.

### `tickPrice()`

```cpp
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
```

- Handles tick price updates.
- Logs the price updates and writes them to shared memory.
- Stores the last price in `l1Prices`.

### `tickSize()`

```cpp
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
```

- Handles tick size updates.
- Logs the size updates and writes them to shared memory.
- Stores the last size in `l1Volumes`.

### `updateMktDepth()`

```cpp
void RealTimeData::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    std::lock_guard<std::mutex> lock(dataMutex);
    processL2Data(position, price, size, side);

    std::ostringstream oss;
    oss << "Update Mkt Depth: " << id << " Position: " << position << " Operation: " << operation << " Side: " << side << " Price: " << price << " Size: " << size;
    STX_LOGI(logger, oss.str());
    writeToSharedMemory(oss.str());
}
```

- Handles market depth updates.
- Logs the updates and writes them to shared memory.
- Processes L2 data by storing it in `l2Data`.

### `updateMktDepthL2()`

```cpp
void RealTimeData::updateMktDepthL2(TickerId id, int position, const std::string &marketMaker, int operation, int side, double price, Decimal size, bool isSmartDepth) {
    std::lock_guard<std::mutex> lock(dataMutex);
    processL2Data(position, price, size, side);

    std::ostringstream oss;
    oss << "Update Mkt Depth L2: " << id << " Position: " << position << " MarketMaker: " << marketMaker << " Operation: " << operation << " Side: " << side << " Price: " << price << " Size: " << size << " SmartDepth: " << isSmartDepth;
    STX_LOGI(logger, oss.str());
    writeToSharedMemory(oss.str());
}
```

- Handles market depth level 2 updates.
- Logs the updates and writes them to shared memory.
- Processes L2 data similarly to `updateMktDepth()`.

### `processL2Data()`

```cpp
void RealTimeData::processL2Data(int position, double price, Decimal size, int side) {
    std::map<std::string, double> data;
    data["Position"] = position;
    data["Price"] = price;
    data["Size"] = size;
    data["Side"] = side;
    l2Data.push_back(data);
}
```

- Processes L2 data by storing it in a map and adding it to `l2Data`.

### `error()`

```cpp
void RealTimeData::error(int id, int errorCode, const std::string &errorString, const std::string &advancedOrderRejectJson) {
    std::lock_guard<std::mutex> lock(dataMutex);

    std::ostringstream oss;
    oss << "Error ID: " << id << " Code: " << errorCode << " Msg: " << errorString;
    STX_LOGE(logger, oss.str());
}
```

- Logs errors received from IB TWS.

### `nextValidId()`

```cpp
void RealTimeData::nextValidId(OrderId orderId) {
    nextOrderId = orderId;
    STX_LOGI(logger, "Next valid order ID: " + std::to_string(orderId));
}
```

- Updates the next valid order ID.

### `aggregateMinuteData()`

```cpp
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

    double bidAskSpread = 0.0, midpoint = 0.0, priceChange = 0.0, l2Depth = 0.0;
    if (!l2Data.empty()) {
        bidAskSpread = l2Data.back()["Price"] - l2Data.front()["Price"];
        midpoint = (l2Data.front()["Price"] + l2Data.back()["Price"]) / 2.0;
        l2Depth = std::accumulate(l2Data.begin(), l2Data.end(), 0.0, [](double sum, const std::map<std::string, double> &data) {
            return sum + data.at("Size");
        });
    }

    double gap = open - yesterdayClose;
    yesterdayClose = close;  // Update for the next day

    std::stringstream combinedData;
    combinedData << getNYTime() << "," << open << "," << high << "," << low << "," << close << "," << volume << ","
                 << bidAskSpread << "," << midpoint << "," << priceChange << "," << l2Depth << "," << gap;

    writeCombinedData(combinedData.str());

    // Clear temporary data
    l1Prices.clear();
    l1Volumes.clear();
    l2Data.clear();
}
```

- Aggregates the data collected in the past minute.
- Calculates open, close, high, low, and volume for L1 data.
- Calculates bid-ask spread, midpoint, price change, and L2 depth for L2 data.
- Calculates the gap between the current open price and yesterday's close price.
- Writes the aggregated data to the combined data file.
- Clears temporary data for the next minute.

### `writeCombinedData()`

```cpp
void RealTimeData::writeCombinedData(const std::string &data) {
    if (combinedDataFile.is_open()) {
        combinedDataFile << data << std::endl;
    }
}
```

- Writes the aggregated data to the combined data file.

### `writeToSharedMemory()`

```cpp
void RealTimeData::writeToSharedMemory(const std::string &data) {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::memcpy(region.get_address(), data.c_str(), data.size());
}
```

- Writes data to shared memory for inter-process communication.

---

This explanation covers the key functionalities and logic implemented in the `RealTimeData.cpp` source file. The class is designed to handle real-time market data retrieval, processing, and storage in an efficient and structured manner, suitable for quantitative analysis and trading strategies.