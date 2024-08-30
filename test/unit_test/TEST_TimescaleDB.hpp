#include <gtest/gtest.h>
#include <memory>
#include <map>
#include <vector>

#include "Logger.h"
#include "TimescaleDB.h"

class TEST_TimescaleDB : public ::testing::Test {
protected:
    std::shared_ptr<Logger> logger;
    std::shared_ptr<TimescaleDB> db;

    void SetUp() override {
        logger = std::make_shared<Logger>("logs/unit_test.log"); 
        db = std::make_shared<TimescaleDB>(logger, "openstx", "openstx", "fs4DGv%xGaE-i5U", "pgm-8vb58v983l2rm0c2do.pgsql.zhangbei.rds.aliyuncs.com", "5432");
    }
};

// 测试 TimescaleDB 连接
TEST_F(TEST_TimescaleDB, ConnectionTest) {
    ASSERT_TRUE(TimescaleDBAccessor::getConnection(*db) != nullptr);  // 使用访问器类访问私有成员 conn
}

// 测试 TimescaleDB 数据库创建
TEST_F(TEST_TimescaleDB, CreateDatabaseTest) {
    TimescaleDBAccessor::callCreateDatabase(*db, "openstx", "openstx", "fs4DGv%xGaE-i5U", "pgm-8vb58v983l2rm0c2do.pgsql.zhangbei.rds.aliyuncs.com", "5432");  // 使用访问器类调用私有方法 createDatabase
    pqxx::connection conn("dbname=testdb user=openstx password=test_password host=localhost port=5432");
    ASSERT_TRUE(conn.is_open());
}

// 测试 TimescaleDB 插入和读取 L1 数据
TEST_F(TEST_TimescaleDB, InsertL1DataTest) {
    std::map<std::string, double> l1Data = {
        {"Bid", 100.5},
        {"Ask", 101.0},
        {"Last", 100.75},
        {"Open", 100.0},
        {"High", 102.0},
        {"Low", 99.5},
        {"Close", 101.25},
        {"Volume", 1500.0}
    };

    std::string datetime = "2024-01-01 12:00:00+00";
    db->insertL1Data(datetime, l1Data);

    pqxx::connection conn("dbname=openstx user=openstx password=test_password host=localhost port=5432");
    pqxx::nontransaction txn(conn);

    pqxx::result res = txn.exec("SELECT * FROM l1_data WHERE datetime = " + txn.quote(datetime) + ";");
    ASSERT_EQ(res.size(), 1);
    ASSERT_EQ(res[0]["bid"].as<double>(), 100.5);
    ASSERT_EQ(res[0]["ask"].as<double>(), 101.0);
}

// 测试 TimescaleDB 插入和读取 L2 数据
TEST_F(TEST_TimescaleDB, InsertL2DataTest) {
    std::vector<std::map<std::string, double>> l2Data = {
        {{"BidPrice", 100.5}, {"BidSize", 500.0}, {"AskPrice", 101.0}, {"AskSize", 600.0}},
        {{"BidPrice", 100.25}, {"BidSize", 300.0}, {"AskPrice", 101.25}, {"AskSize", 400.0}}
    };

    std::string datetime = "2024-01-01 12:00:00+00";
    db->insertL2Data(datetime, l2Data);

    pqxx::connection conn("dbname=openstx user=openstx password=test_password host=localhost port=5432");
    pqxx::nontransaction txn(conn);

    pqxx::result res = txn.exec("SELECT * FROM l2_data WHERE datetime = " + txn.quote(datetime) + ";");
    ASSERT_EQ(res.size(), 2);
    ASSERT_EQ(res[0]["bid_price"].as<double>(), 100.5);
    ASSERT_EQ(res[0]["ask_price"].as<double>(), 101.0);
}

// 测试 TimescaleDB 插入和读取 Feature 数据
TEST_F(TEST_TimescaleDB, InsertFeatureDataTest) {
    std::map<std::string, double> features = {
        {"Gap", 0.75},
        {"TodayOpen", 100.0},
        {"TotalL2Volume", 2000.0},
        {"RSI", 55.0},
        {"MACD", 0.1},
        {"VWAP", 100.25}
    };

    std::string datetime = "2024-01-01 12:00:00+00";
    db->insertFeatureData(datetime, features);

    pqxx::connection conn("dbname=openstx user=openstx password=test_password host=localhost port=5432");
    pqxx::nontransaction txn(conn);

    pqxx::result res = txn.exec("SELECT * FROM feature_data WHERE datetime = " + txn.quote(datetime) + ";");
    ASSERT_EQ(res.size(), 1);
    ASSERT_EQ(res[0]["gap"].as<double>(), 0.75);
    ASSERT_EQ(res[0]["vwap"].as<double>(), 100.25);
}
