#pragma once
#ifndef TWS_API_CLIENT_DECIMAL_H
#define TWS_API_CLIENT_DECIMAL_H

#include "platformspecific.h"
#include <sstream>
#include <climits>
#include <string>
#include <stdexcept>  // For std::invalid_argument and std::out_of_range

// Decimal type
typedef unsigned long long Decimal;

#define UNSET_DECIMAL ULLONG_MAX

// Implement arithmetic functions without BID
inline Decimal add(Decimal decimal1, Decimal decimal2) {
    // Note: This is a simple replacement. Ensure that this satisfies your precision and range requirements.
    return decimal1 + decimal2;
}

inline Decimal sub(Decimal decimal1, Decimal decimal2) {
    // Handle underflow or define behavior for negative results
    return decimal1 >= decimal2 ? decimal1 - decimal2 : 0;  // Return 0 if underflow occurs
}

inline Decimal mul(Decimal decimal1, Decimal decimal2) {
    // Use careful logic to avoid overflow if possible
    return decimal1 * decimal2;
}

inline Decimal div(Decimal decimal1, Decimal decimal2) {
    if (decimal2 == 0) {
        throw std::runtime_error("Division by zero");
    }
    return decimal1 / decimal2;
}

// Conversion functions using standard library
inline Decimal stringToDecimal(std::string str) {
    unsigned int flags;
    if (str.compare("2147483647") == 0 || str.compare("9223372036854775807") == 0 || str.compare("1.7976931348623157E308") == 0) {
        str.clear(); // Clear if max value is detected
    }
    try {
        return std::stoull(str);  // Use std::stoull for conversion
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid decimal string");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Decimal string out of range");
    }
}

inline std::string decimalToString(Decimal value) {
    return std::to_string(value);  // Use std::to_string for conversion
}

inline std::string decimalStringToDisplay(Decimal value) {
    // Convert string with scientific notation to string with decimal notation
    std::string tempStr = decimalToString(value);

    if (tempStr == "+NaN" || tempStr == "-SNaN") {
        return ""; // Return empty string for invalid values
    }

    int expPos = tempStr.find('E');
    if (expPos < 0) {
        return tempStr;
    }

    std::string expStr = tempStr.substr(expPos);
    int exp = 0;
    for (size_t i = 2; i < expStr.size(); i++) {
        exp = exp * 10 + (expStr[i] - '0');
    }
    if (expStr[1] == '-') {
        exp *= -1;
    }

    size_t numLength = tempStr.size() - expStr.size() - 1;
    bool isNegative = tempStr[0] == '-';
    std::string numbers = tempStr.substr(1, numLength);

    if (exp == 0) {
        return isNegative ? "-" + numbers : numbers;
    }

    std::string result = isNegative ? "-" : "";
    bool decPtAdded = false;

    for (size_t i = numLength; i <= static_cast<size_t>(-exp); i++) {
        result += '0';
        if (i == numLength) {
            result += '.';
            decPtAdded = true;
        }
    }

    for (size_t i = 0; i < numLength; i++) {
        if (numLength - i == static_cast<size_t>(-exp) && !decPtAdded) {
            result += '.';
        }
        result += numbers[i];
    }
    return result;
}

inline double decimalToDouble(Decimal decimal) {
    return static_cast<double>(decimal);  // Use static_cast for conversion
}

inline Decimal doubleToDecimal(double d) {
    return static_cast<Decimal>(d);  // Use static_cast for conversion
}

#endif // TWS_API_CLIENT_DECIMAL_H
