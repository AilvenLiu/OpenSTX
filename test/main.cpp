#include <iostream>
#include <pqxx/pqxx>

int main() {
    try {
        // Connect to the database
        pqxx::connection conn("dbname=test user=test_user password=test_password host=localhost port=5432");

        if (conn.is_open()) {
            std::cout << "Connected to database successfully: " << conn.dbname() << std::endl;
        } else {
            std::cerr << "Failed to connect to the database" << std::endl;
            return 1;
        }

        // Create a transaction
        pqxx::work txn(conn);

        // Execute a simple query
        pqxx::result res = txn.exec("SELECT version();");

        // Print out the PostgreSQL version
        for (auto row : res) {
            std::cout << "PostgreSQL version: " << row[0].c_str() << std::endl;
        }

        // Commit the transaction
        txn.commit();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
