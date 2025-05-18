// Publisher/Subscriber Pattern (also known as Pub/Sub Pattern)
//
// The Publisher/Subscriber Pattern is a design pattern that allows you to define a one-to-many dependency between objects so that when one object changes state, all its dependents are notified and updated automatically.
//
// This publisher's job is to specifically watch for changes to stk_q's or .csv files
// when it sees a change, it will notify the subscribers
//
// In the Pub/Sub terminology:
// - PublisherBase class acts as the "Publisher"
// - Publisher interface defines the "Subscriber" contract
// - Concrete publishers (StockQueuePublisher, CSVFilePublisher) are specialized Publishers
// - Concrete implementations of Publisher interface (like Subscriber class) are Subscribers

#ifndef PUBLISHER_HPP
#define PUBLISHER_HPP

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include "../stk_q/stk_q.hpp"

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

    // Concrete STK_Q publisher (specialized Publisher watching stock queue)
    class StockQueuePublisher : public PublisherBase {
        private:
            std::shared_ptr<stk_q::STK_Q> m_queue;
            bool m_running;
            std::thread m_watchThread;
            
        public:
            StockQueuePublisher(std::shared_ptr<stk_q::STK_Q> queue);
            ~StockQueuePublisher();
            
            // Start watching the queue for changes to publish
            void startWatching();
            
            // Stop watching the queue
            void stopWatching();
    };

    // CSV File Publisher - watches for changes in a CSV file
    // (Specialized Publisher that monitors a CSV file and publishes changes)
    class CSVFilePublisher : public PublisherBase {
        private:
            std::string m_filePath;
            std::unordered_set<std::string> m_previousContent;
            bool m_running;
            std::thread m_watchThread;
            std::mutex m_contentMutex;
            unsigned int m_checkIntervalMs;
            
            // Read current file content
            std::unordered_set<std::string> readFileContent();
            
            // Check for changes and notify subscribers (i.e., publish events)
            void checkForChanges();
            
        public:
            CSVFilePublisher(const std::string& filePath, unsigned int checkIntervalMs = 1000);
            ~CSVFilePublisher();
            
            // Start watching the file for changes to publish
            void startWatching();
            
            // Stop watching the file
            void stopWatching();
            
            // Manually trigger a check for changes and publish if found
            void checkNow();
            
            // Get the file path being watched
            const std::string& getFilePath() const { return m_filePath; }
    };
    
    // Utility functions for CSV file operations
    
    // Function to modify a CSV file (add or remove entries)
    void modifyCSVFile(const std::string& filePath, const std::string& action, const std::string& symbol = "");
    
    // Function to modify a CSV file with error handling
    bool modify_csv_file(const std::string& filePath, const std::string& action, const std::string& symbol = "");
    
    // Function to print the contents of a CSV file
    void printCSVContents(const std::string& filePath);
    
    // Function to create and start a CSV file publisher
    std::shared_ptr<CSVFilePublisher> createCSVPublisher(const std::string& filePath, unsigned int checkIntervalMs = 500);

} // namespace publisher

#endif 