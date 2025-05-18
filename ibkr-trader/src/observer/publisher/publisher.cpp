#include "publisher.hpp"
#include <iostream>

namespace publisher {

    // PublisherBase (Publisher) methods implementation
    
    // Register a subscriber to receive notifications
    void PublisherBase::addSubscriber(std::shared_ptr<Publisher> subscriber) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.push_back(subscriber);
    }
    
    // Remove a subscriber from the notification list
    void PublisherBase::removeSubscriber(std::shared_ptr<Publisher> subscriber) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.erase(
            std::remove(m_subscribers.begin(), m_subscribers.end(), subscriber),
            m_subscribers.end()
        );
    }
    
    // Publish an event/notification to all subscribers
    void PublisherBase::notifySubscribers(const std::string& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& subscriber : m_subscribers) {
            subscriber->update(data);  // Call the subscriber's update method
        }
    }

} // namespace publisher 