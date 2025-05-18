#include "observer.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <sstream>

namespace observer {

// Helper function to generate a unique ID
std::string Observer::generateUniqueId() {
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    
    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    
    return ss.str();
}

// Destructor - ensures all observation is stopped
Observer::~Observer() {
    stopAll();
}

// Register a custom publisher with a callback function
std::string Observer::registerPublisher(
    std::shared_ptr<publisher::PublisherBase> publisher,
    NotificationCallback callback
) {
    // Generate a unique ID for this observation
    std::string observerId = generateUniqueId();
    
    // Create a callback subscriber
    auto subscriber = std::make_shared<CallbackSubscriber>(callback);
    
    // Connect the subscriber to the publisher
    publisher->addSubscriber(subscriber);
    
    // Store the publisher and subscriber
    m_publishers[observerId] = publisher;
    m_subscribers[observerId] = subscriber;
    
    std::cout << "Started observing with ID: " << observerId << std::endl;
    
    // Return the ID for future reference
    return observerId;
}

// Stop observing a specific publisher
bool Observer::stopObserving(const std::string& observerId) {
    auto publisherIt = m_publishers.find(observerId);
    auto subscriberIt = m_subscribers.find(observerId);
    
    if (publisherIt != m_publishers.end() && subscriberIt != m_subscribers.end()) {
        // Remove subscriber from publisher
        publisherIt->second->removeSubscriber(subscriberIt->second);
        
        // Remove from both maps
        m_publishers.erase(publisherIt);
        m_subscribers.erase(subscriberIt);
        
        std::cout << "Stopped observing with ID: " << observerId << std::endl;
        return true;
    }
    
    return false;
}

// Stop observing all publishers
void Observer::stopAll() {
    for (auto& pair : m_publishers) {
        auto subscriberIt = m_subscribers.find(pair.first);
        if (subscriberIt != m_subscribers.end()) {
            pair.second->removeSubscriber(subscriberIt->second);
        }
    }
    
    m_publishers.clear();
    m_subscribers.clear();
    
    std::cout << "Stopped all observations" << std::endl;
}

} // namespace observer 