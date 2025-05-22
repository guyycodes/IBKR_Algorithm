#include "account_summary.hpp"

namespace account_summary {

void AccountSummaryManager::updateAccountSummary(int reqId, const std::string& account, const std::string& tag, 
                                                 const std::string& value, const std::string& currency) {
    accountData[account][tag] = AccountSummaryData(reqId, account, tag, value, currency);
}

std::string AccountSummaryManager::getValue(const std::string& account, const std::string& tag) const {
    auto accountIt = accountData.find(account);
    if (accountIt != accountData.end()) {
        auto tagIt = accountIt->second.find(tag);
        if (tagIt != accountIt->second.end()) {
            return tagIt->second.value;
        }
    }
    return "";
}

double AccountSummaryManager::getNumericValue(const std::string& account, const std::string& tag) const {
    auto accountIt = accountData.find(account);
    if (accountIt != accountData.end()) {
        auto tagIt = accountIt->second.find(tag);
        if (tagIt != accountIt->second.end()) {
            return tagIt->second.getNumericValue();
        }
    }
    return 0.0;
}

std::unordered_map<std::string, AccountSummaryData> AccountSummaryManager::getAccountData(const std::string& account) const {
    auto it = accountData.find(account);
    if (it != accountData.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> AccountSummaryManager::getAccounts() const {
    std::vector<std::string> accounts;
    for (const auto& pair : accountData) {
        accounts.push_back(pair.first);
    }
    return accounts;
}

void AccountSummaryManager::printAllData() const {
    for (const auto& accountPair : accountData) {
        std::cout << "\n========== ACCOUNT: " << accountPair.first << " ==========\n";
        for (const auto& tagPair : accountPair.second) {
            tagPair.second.print();
        }
    }
}

void AccountSummaryManager::clear() {
    accountData.clear();
}

} // namespace account_summary