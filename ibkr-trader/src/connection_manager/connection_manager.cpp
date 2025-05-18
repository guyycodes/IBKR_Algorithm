// connection_manager.cpp
// This file integrates the api_functions to the IBKR API and the config together in an easy to implement manager

#include "connection_manager.hpp"
#include <iostream>
#include <memory>
#include <thread>

namespace connection_manager {

    ConnectionManager::ConnectionManager() 
        : m_trader(new connection::IBKRTrader())
        , m_connected(false)
    {
        m_api_functions = std::make_unique<ibkr_api_functions::API_Functions>(*m_trader);
    }

    ConnectionManager::~ConnectionManager() {
        if (m_connected) {
            disconnect();
        }
    }

    bool ConnectionManager::connect() {
        if (m_connected) {
            std::cout << "Already connected to IBKR API." << std::endl;
            return true;
        }

        if (m_trader->connect()) {
            m_reader = m_trader->createReader();
            m_msgProcessingThread = m_trader->startMessageProcessing(m_reader);
            m_connected = true;
            return true;
        }

        return false;
    }

    void ConnectionManager::disconnect() {
        if (!m_connected) {
            std::cout << "Not connected to IBKR API." << std::endl;
            return;
        }

        m_trader->disconnect();
        
        if (m_msgProcessingThread.joinable()) {
            m_msgProcessingThread.join();
        }
        
        m_connected = false;
    }

} // namespace connection_manager