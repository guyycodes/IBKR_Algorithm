#include "subscriber.hpp"

namespace subscriber {

// Constructor implementation
Subscriber::Subscriber(const std::string& name) : m_name(name) {}

// Implement the Publisher interface
void Subscriber::update(const std::string& data) {
    // Process the notification data
    std::cout << "[" << m_name << "] Received notification: " << data << std::endl;
}

// Set subscriber name
void Subscriber::setName(const std::string& name) {
    m_name = name;
}

// Get subscriber name
const std::string& Subscriber::getName() const {
    return m_name;
}

} // namespace subscriber 