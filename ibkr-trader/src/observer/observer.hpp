// observer.hpp
//
// This file defines the Observer class which serves as a facade for the Publisher-Subscriber pattern.
// The Observer class simplifies using the pub/sub pattern by encapsulating both publisher and subscriber logic.
// It provides a clean interface for setting up monitoring and handling notifications.

#ifndef OBSERVER_HPP
#define OBSERVER_HPP

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include "publisher/publisher.hpp"
#include "subscriber/subscriber.hpp"

namespace observer {

// Callback function type for notification events
using NotificationCallback = std::function<void(const std::string&)>;

// Observer class - provides a simplified interface to the pub/sub pattern
class Observer {
private:
    // Publishers
    std::unordered_map<std::string, std::shared_ptr<publisher::PublisherBase>> m_publishers;
    
    // Custom subscriber that forwards notifications to callbacks
    class CallbackSubscriber : public publisher::Publisher {
    private:
        NotificationCallback m_callback;
        
    public:
        explicit CallbackSubscriber(NotificationCallback callback) : m_callback(std::move(callback)) {}
        
        void update(const std::string& data) override {
            if (m_callback) {
                m_callback(data);
            }
        }
    };
    
    // Map of subscribers
    std::unordered_map<std::string, std::shared_ptr<CallbackSubscriber>> m_subscribers;
    
    // Helper function to generate a unique ID
    std::string generateUniqueId();
    
public:
    Observer() = default;
    ~Observer();
    
    // Register a custom publisher with a callback function
    // Returns a unique ID for this observation that can be used to stop observing
    std::string registerPublisher(
        std::shared_ptr<publisher::PublisherBase> publisher,
        NotificationCallback callback
    );
    
    // Stop observing a specific publisher
    bool stopObserving(const std::string& observerId);
    
    // Stop observing all publishers
    void stopAll();
};

} // namespace observer

#endif // OBSERVER_HPP 