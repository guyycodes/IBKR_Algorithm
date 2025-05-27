// connection_manager.cpp
// This file integrates the api_functions to the IBKR API and the config together in an easy to implement manager

#include "connection_manager.hpp"
#include "../util/app_state/app_state.hpp"
#include <iostream>
#include <memory>
#include <thread>

namespace connection_manager {

    ConnectionManager::ConnectionManager() 
        : m_trader(new connection::IBKRTrader())
        , m_connected(false)
    {

    }

    ConnectionManager::~ConnectionManager() {
        if (m_connected) {
            disconnect();
        }
    }

    bool ConnectionManager::connect(int clientId, const std::string& symbol, const Contract& contract) {
        if (m_connected) {
            std::cout << "Already connected to IBKR API." << std::endl;
            return true;
        }

        if (m_trader->connect(clientId, symbol, contract)) {
            m_reader = m_trader->createReader();
            // Instead of directly creating the thread, we'll request AppState to create and manage it
            // Only create the thread directly if m_threadId is not set (not registered with AppState)
            if (m_threadId.empty()) {
                m_msgProcessingThread = m_trader->startMessageProcessing(m_reader);
            } else {
                // If we have a thread ID, let AppState know this thread will be running
                auto& appState = app_state::AppState::getInstance();
                // Here we would register the message processing thread with AppState
                // This would require modifications to AppState to handle non-model threads
            }
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
        
        // If we have a thread ID, request AppState to stop it
        if (!m_threadId.empty()) {
            auto& appState = app_state::AppState::getInstance();
            // Here we would request AppState to stop this thread
            // This would require modifications to AppState to handle non-model threads
        } else {
            // Otherwise, handle the thread directly
            if (m_msgProcessingThread.joinable()) {
                m_msgProcessingThread.join();
            }
        }
        
        m_connected = false;
    }

} // namespace connection_manager