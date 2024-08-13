#include <pqxx/pqxx>
#include <iostream>
#include <memory>

// 简单的日志类（可以替换为项目中的日志系统）
class Logger {
public:
    void info(const std::string& message) {
        std::cout << "[INFO] " << message << std::endl;
    }

    void error(const std::string& message) {
        std::cout << "[ERROR] " << message << std::endl;
    }
};

class TestTimescaleDB {
public:
    TestTimescaleDB(const std::shared_ptr<Logger>& logger, const std::string &_dbname, const std::string &_user, const std::string &_password, const std::string &_host, const std::string &_port)
        : logger(logger), conn(nullptr), dbname(_dbname), user(_user), password(_password), host(_host), port(_port) {
        connect();
        enableTimescaleExtension();
        createTables();
    }

    ~TestTimescaleDB() {
        disconnect();
        dropDatabase();
    }

    void runTests() {
        insertTestData();
        queryTestData();
        updateTestData();
        deleteTestData();
    }

private:
    std::shared_ptr<Logger> logger;
    pqxx::connection *conn;
    std::string dbname, user, password, host, port;

    void connect() {
        try {
            std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;

            try {
                conn = new pqxx::connection(connectionString);
                if (conn->is_open()) {
                    logger->info("Connected to TimescaleDB: " + dbname);
                } else {
                    logger->error("Failed to connect to TimescaleDB: " + dbname);
                    createDatabase();
                }
            } catch (const pqxx::broken_connection& e) {
                logger->info("Database does not exist. Attempting to create database: " + dbname);
                createDatabase();
            }
        } catch (const std::exception &e) {
            logger->error("Error initializing TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void createDatabase() {
        try {
            // Connect to the default "postgres" database to create a new database
            std::string adminConnectionString = "dbname=postgres user=" + user + " password=" + password + " host=" + host + " port=" + port;
            pqxx::connection adminConn(adminConnectionString);

            if (adminConn.is_open()) {
                pqxx::nontransaction txn(adminConn);  // Use nontransaction to avoid creating a transaction block

                // Create the database using a direct exec command
                txn.exec("CREATE DATABASE " + dbname + " TABLESPACE openstx_space;");

                logger->info("Database created successfully in tablespace openstx_space.");

                // Reconnect to the newly created database
                std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;
                conn = new pqxx::connection(connectionString);
                if (conn->is_open()) {
                    logger->info("Connected to TimescaleDB: " + dbname);
                } else {
                    logger->error("Failed to connect to TimescaleDB after creation: " + dbname);
                    cleanupAndExit();
                }
            } else {
                logger->error("Failed to connect to the PostgreSQL server to create the database.");
                cleanupAndExit();
            }
        } catch (const std::exception &e) {
            logger->error("Error creating TimescaleDB database: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void enableTimescaleExtension() {
        try {
            pqxx::work txn(*conn);
            txn.exec("CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;");
            txn.commit();
            logger->info("TimescaleDB extension enabled.");
        } catch (const std::exception &e) {
            logger->error("Error enabling TimescaleDB extension: " + std::string(e.what()));
            reconnect();
        }
    }

    void createTables() {
        try {
            pqxx::work txn(*conn);

            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS test_data (
                    id SERIAL PRIMARY KEY,
                    name TEXT NOT NULL,
                    value DOUBLE PRECISION,
                    created_at TIMESTAMPTZ DEFAULT NOW()
                );
            )");

            txn.commit();
            logger->info("Table 'test_data' created or verified successfully.");
        } catch (const std::exception &e) {
            logger->error("Error creating table in TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void insertTestData() {
        try {
            pqxx::work txn(*conn);

            txn.exec("INSERT INTO test_data (name, value) VALUES ('test1', 123.45);");
            txn.exec("INSERT INTO test_data (name, value) VALUES ('test2', 678.90);");

            txn.commit();
            logger->info("Inserted test data into 'test_data' table.");
        } catch (const std::exception &e) {
            logger->error("Error inserting test data into TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void queryTestData() {
        try {
            pqxx::work txn(*conn);

            pqxx::result r = txn.exec("SELECT * FROM test_data;");

            logger->info("Querying test data from 'test_data' table:");
            for (auto row : r) {
                logger->info(
                    std::string("id = ") + row["id"].c_str() + 
                    ", name = " + row["name"].c_str() + 
                    ", value = " + row["value"].c_str() + 
                    ", created_at = " + row["created_at"].c_str()
                );
            }
        } catch (const std::exception &e) {
            logger->error("Error querying test data from TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void updateTestData() {
        try {
            pqxx::work txn(*conn);

            txn.exec("UPDATE test_data SET value = 999.99 WHERE name = 'test1';");

            txn.commit();
            logger->info("Updated test data in 'test_data' table.");
        } catch (const std::exception &e) {
            logger->error("Error updating test data in TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void deleteTestData() {
        try {
            pqxx::work txn(*conn);

            txn.exec("DELETE FROM test_data WHERE name = 'test2';");

            txn.commit();
            logger->info("Deleted test data from 'test_data' table.");
        } catch (const std::exception &e) {
            logger->error("Error deleting test data from TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void reconnect() {
        try {
            if (conn) {
                delete conn;
                conn = nullptr;
            }

            std::string connectionString = "dbname=" + dbname + " user=" + user + " password=" + password + " host=" + host + " port=" + port;
            conn = new pqxx::connection(connectionString);

            if (conn->is_open()) {
                logger->info("Reconnected to TimescaleDB: " + dbname);
            } else {
                logger->error("Failed to reconnect to TimescaleDB: " + dbname);
                cleanupAndExit();
            }
        } catch (const std::exception &e) {
            logger->error("Error reconnecting to TimescaleDB: " + std::string(e.what()));
            cleanupAndExit();
        }
    }

    void dropDatabase() {
        try {
            disconnect();
            std::string adminConnectionString = "dbname=postgres user=" + user + " password=" + password + " host=" + host + " port=" + port;
            pqxx::connection adminConn(adminConnectionString);
            pqxx::nontransaction txn(adminConn);

            txn.exec("DROP DATABASE IF EXISTS " + dbname + ";");
            logger->info("Dropped database: " + dbname);
        } catch (const std::exception &e) {
            logger->error("Error dropping database: " + std::string(e.what()));
        }
    }

    void disconnect() {
        if (conn) {
            delete conn;
            conn = nullptr;
            logger->info("Disconnected from TimescaleDB.");
        }
    }

    void cleanupAndExit() {
        disconnect();
        logger->info("Exiting program due to error.");
        exit(EXIT_FAILURE);
    }
};

int main() {
    std::shared_ptr<Logger> logger = std::make_shared<Logger>();

    std::string dbname = "test_timescale_db";  // 使用新的数据库名称
    std::string user = "openstx";
    std::string password = "test_password";
    std::string host = "localhost";
    std::string port = "5432";

    TestTimescaleDB tester(logger, dbname, user, password, host, port);
    tester.runTests();

    return 0;
}