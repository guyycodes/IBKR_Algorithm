// subscriber.hpp
// 
// This file defines the Subscriber class, which acts as a Subscriber in the pub/sub pattern.
// A Subscriber registers with a Publisher and receives notifications when the Publisher's state changes.

#ifndef SUBSCRIBER_HPP
#define SUBSCRIBER_HPP

#include <string>
#include <iostream>
#include <memory>
#include "../publisher/publisher.hpp"

namespace subscriber {

// Subscriber class implements the Publisher interface
// In pub/sub terminology, this makes it a concrete Subscriber that receives
// notifications from Publishers
class Subscriber : public publisher::Publisher {
private:
    std::string m_name;

public:
    Subscriber(const std::string& name = "DefaultSubscriber");
    
    // Implement the Publisher/Subscriber interface
    // This method is called when a Publisher sends a notification
    void update(const std::string& data) override;
    
    // Set subscriber name
    void setName(const std::string& name);
    
    // Get subscriber name
    const std::string& getName() const;
}; // class Subscriber

} // namespace subscriber

#endif // SUBSCRIBER_HPP 