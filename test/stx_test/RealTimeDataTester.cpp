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

#include <iostream>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <sstream>
#include "EReaderOSSignal.h"
#include "RealTimeDataTester.hpp"

RealTimeDataTester::RealTimeDataTester()
    : m_osSignal(std::make_unique<EReaderOSSignal>())
    , m_pClient(std::make_unique<EClientSocket>(this, m_osSignal.get()))
    , m_running(false)
    , m_tickerId(0) {
    
    // Initialize the contract for SPY
    m_contract.symbol = "SPY";
    m_contract.secType = "STK";
    m_contract.exchange = "SMART";
    m_contract.currency = "USD";
}

RealTimeDataTester::~RealTimeDataTester() {
    stop();
}

void RealTimeDataTester::start() {
    const int MAX_RETRIES = 5;
    int retries = 0;

    while (retries < MAX_RETRIES && !m_running) {
        if (connectToTWS()) {
            m_running = true;
            
            // Add a longer delay before requesting market data
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            requestMarketData();
            requestMarketDepth();  // Request market depth data

            // Wait for market data (e.g., 2 minutes)
            // std::this_thread::sleep_for(std::chrono::minutes(2));

            m_readerThread = std::thread([this]() {
                while (m_running && m_pClient->isConnected()) {
                    m_osSignal->waitForSignal();
                    std::lock_guard<std::mutex> lock(m_readerMutex);
                    m_pReader->processMsgs();
                }
                if (m_running) {
                    logMessage("Connection to TWS closed unexpectedly.");
                    m_running = false;
                }
            });

            // Start a thread to periodically check data health
            m_healthCheckThread = std::thread([this]() {
                while (m_running) {
                    std::this_thread::sleep_for(std::chrono::minutes(1));
                    checkDataHealth();
                }
            });

            // Wait for the reader thread to finish
            if (m_readerThread.joinable()) {
                m_readerThread.join();
            }

            // Wait for the health check thread to finish
            if (m_healthCheckThread.joinable()) {
                m_healthCheckThread.join();
            }

            logMessage("Tester stopped.");
        } else {
            logMessage("Failed to connect to TWS.");
        }

        retries++;
        if (retries < MAX_RETRIES && !m_running) {
            logMessage("Retrying in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    if (retries == MAX_RETRIES && !m_running) {
        logMessage("Max retries reached. Exiting.");
    }
}

void RealTimeDataTester::stop() {
    if (!m_running) return;

    m_running = false;
    m_pClient->cancelMktData(m_tickerId);
    m_pClient->eDisconnect();

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }

    logMessage("Tester stopped.");
}

bool RealTimeDataTester::connectToTWS() {
    if (!m_pClient->eConnect("127.0.0.1", 7497, 0)) {
        logMessage("Failed to connect to TWS.");
        return false;
    }

    logMessage("Connected to TWS.");
    m_pReader = std::make_unique<EReader>(m_pClient.get(), m_osSignal.get());
    m_pReader->start();

    // Add a delay after connecting
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Check if still connected after the delay
    if (!m_pClient->isConnected()) {
        logMessage("Lost connection to TWS after initial connect.");
        return false;
    }

    return true;
}

void RealTimeDataTester::requestMarketData() {
    m_tickerId = 1;  // Use a unique ticker ID
    m_pClient->reqMktData(m_tickerId, m_contract, "", false, false, TagValueListSPtr());
    logMessage("Requested market data for " + m_contract.symbol);
}

void RealTimeDataTester::requestMarketDepth() {
    int depthRows = 10;  // Number of rows of market depth to request

    // Modify the contract to request data specifically from NYSE
    m_contract.primaryExchange = "NYSE";
    m_pClient->reqMktDepth(++m_tickerId, m_contract, depthRows, true, TagValueListSPtr());
    logMessage("Requested market depth for " + m_contract.symbol + " from NYSE");

    // Optionally, request from another exchange if you have the subscription
    m_contract.primaryExchange = "NASDAQ";
    m_pClient->reqMktDepth(++m_tickerId, m_contract, depthRows, true, TagValueListSPtr());
    logMessage("Requested market depth for " + m_contract.symbol + " from NASDAQ");
}

void RealTimeDataTester::processMessages() {
    while (m_running) {
        m_osSignal->waitForSignal();
        std::lock_guard<std::mutex> lock(m_readerMutex);
        m_pReader->processMsgs();
    }
}

void RealTimeDataTester::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Log all ticks
    logAllTicks(tickerId, field, price, attrib);
    
    if (field == TickType::LAST || field == TickType::DELAYED_LAST) {
        m_l1Prices.push_back(price);
        logMessage("L1 Price: " + std::to_string(price));
    }
}

void RealTimeDataTester::tickSize(TickerId tickerId, TickType field, Decimal size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (field == TickType::LAST_SIZE || field == TickType::DELAYED_LAST_SIZE) {
        m_l1Volumes.push_back(size);
        logMessage("L1 Volume: " + std::to_string(size));
    }
}

void RealTimeDataTester::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (operation == 0) {  // Insert
        m_l2Data.emplace_back(price, size);
        logMessage("Inserted L2 Data: Position " + std::to_string(position) + ", Price " + std::to_string(price) + ", Size " + std::to_string(size));
    } else if (operation == 1) {  // Update
        if (position < m_l2Data.size()) {
            m_l2Data[position] = std::make_pair(price, size);
            logMessage("Updated L2 Data: Position " + std::to_string(position) + ", Price " + std::to_string(price) + ", Size " + std::to_string(size));
        }
    } else if (operation == 2) {  // Delete
        if (position < m_l2Data.size()) {
            m_l2Data.erase(m_l2Data.begin() + position);
            logMessage("Deleted L2 Data: Position " + std::to_string(position));
        }
    }
}

// Add this function to the RealTimeDataTester class
std::string RealTimeDataTester::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void RealTimeDataTester::logAllTicks(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
    logMessage("Tick received - ID: " + std::to_string(tickerId) + ", Type: " + std::to_string(static_cast<int>(field)) + ", Price: " + std::to_string(price));
}
// Update the error function
void RealTimeDataTester::error(int id, int errorCode, const std::string& errorString, const std::string& advancedOrderRejectJson) {
    std::cout << "[" << getCurrentTimestamp() << "] Error " << errorCode << ": " << errorString << std::endl;
    if (!advancedOrderRejectJson.empty()) {
        std::cout << "Advanced order reject: " << advancedOrderRejectJson << std::endl;
    }
}

void RealTimeDataTester::connectionClosed() {
    logMessage("Connection to TWS closed.");
    m_running = false;
}

void RealTimeDataTester::logMessage(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "[%Y-%m-%d %H:%M:%S] ");
    std::cout << ss.str() << message << std::endl;
}

void RealTimeDataTester::checkDataHealth() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_l2Data.empty()) {
        logMessage("L2 data is empty. Attempting to re-request market depth data.");
        requestMarketDepth();
    } else {
        logMessage("L2 data is present. Number of entries: " + std::to_string(m_l2Data.size()));
    }
}

// Main function to run the tester
int main() {
    RealTimeDataTester tester;
    tester.start();

    // Run for 5 minutes
    std::this_thread::sleep_for(std::chrono::minutes(5));

    tester.stop();
    return 0;
}
