// Publisher/Subscriber Pattern (also known as Pub/Sub Pattern)
//
// The Publisher/Subscriber Pattern is a design pattern that allows you to define a one-to-many dependency 
// between objects so that when one object changes state, all its dependents are notified and updated automatically.

#ifndef PUBLISHER_HPP
#define PUBLISHER_HPP

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <mutex>

namespace publisher {

    // Forward declaration
    class PublisherBase;

    // Publisher interface (acts as the Subscriber in pub/sub pattern)
    // Defines how subscribers receive notifications from publishers
    class Publisher {
        public:
            virtual void update(const std::string& data) = 0;
            virtual ~Publisher() = default;
    };

    // PublisherBase interface (acts as the Publisher in pub/sub pattern)
    // Maintains a list of subscribers and notifies them of changes
    class PublisherBase {
        private:
            std::vector<std::shared_ptr<Publisher>> m_subscribers;  // List of subscribers
            std::mutex m_mutex;
            
        public:
            virtual ~PublisherBase() = default;
            
            // Add subscriber to notification list (subscriber registration)
            void addSubscriber(std::shared_ptr<Publisher> subscriber);
            
            // Remove subscriber from notification list (subscriber unregistration)
            void removeSubscriber(std::shared_ptr<Publisher> subscriber);
            
            // Notify all subscribers (publish event to all subscribers)
            void notifySubscribers(const std::string& data);
    };

} // namespace publisher

#endif // PUBLISHER_HPP 