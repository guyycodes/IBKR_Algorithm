// account_summary.hpp
#ifndef ACCOUNT_SUMMARY_HPP
#define ACCOUNT_SUMMARY_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>

namespace account_summary {

struct AccountSummaryData {
    int reqId;
    std::string account;
    std::string tag;
    std::string value;
    std::string currency;
    
    // Constructor
    AccountSummaryData(int reqId, const std::string& account, const std::string& tag, 
                      const std::string& value, const std::string& currency)
        : reqId(reqId), account(account), tag(tag), value(value), currency(currency) {}
    
    // Default constructor
    AccountSummaryData() : reqId(0) {}
    
    // Helper method to convert value to double (useful for numeric tags)
    double getNumericValue() const {
        try {
            return std::stod(value);
        } catch (const std::exception&) {
            return 0.0;
        }
    }
    
    // Pretty print method
    void print() const {
        std::cout << "\n===== ACCOUNT SUMMARY DATA =====\n";
        std::cout << "reqId: " << reqId << "\n";
        std::cout << "account: " << account << "\n";
        std::cout << "tag: " << tag << "\n";
        std::cout << "value: " << value << "\n";
        std::cout << "currency: " << currency << "\n";
        std::cout << "===============================\n";
    }
    
    // Comparison operators for sorting/searching
    bool operator==(const AccountSummaryData& other) const {
        return reqId == other.reqId && account == other.account && 
               tag == other.tag && value == other.value && currency == other.currency;
    }
};

// Container class to manage multiple account summary entries
class AccountSummaryManager {
private:
    // Map of account -> (tag -> AccountSummaryData)
    std::unordered_map<std::string, std::unordered_map<std::string, AccountSummaryData>> accountData;
    
public:
    // Add or update account summary data
    void updateAccountSummary(int reqId, const std::string& account, const std::string& tag, 
                             const std::string& value, const std::string& currency);
    
    // Get specific account summary value
    std::string getValue(const std::string& account, const std::string& tag) const;
    
    // Get numeric value
    double getNumericValue(const std::string& account, const std::string& tag) const;
    
    // Get all data for an account
    std::unordered_map<std::string, AccountSummaryData> getAccountData(const std::string& account) const;
    
    // Get all accounts
    std::vector<std::string> getAccounts() const;
    
    // Print all account data
    void printAllData() const;
    
    // Clear all data
    void clear();
};

} // namespace account_summary

#endif // ACCOUNT_SUMMARY_HPP