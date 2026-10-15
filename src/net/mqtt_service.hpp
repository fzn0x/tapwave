// The MQTT connection, on ESP-IDF's esp-mqtt client (ARCHITECTURE.md section 12).
#pragma once

#include <mqtt_client.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "core/mqtt_layout.hpp"

namespace tapwave::net {

class MqttService {
public:
    struct Settings {
        std::string host;
        int port = 1883;
        bool tls = false;
        std::string username;
        std::string password;
        std::string client_id;
        std::string topic_prefix;  // expanded
        std::string discovery_prefix;
    };

    ~MqttService() { stop(); }

    void start(const Settings& settings);
    void stop();

    bool started() const { return client_ != nullptr; }
    bool connected() const { return connected_.load(); }
    std::string last_error() const;
    // A stable code for last_error(): "unreachable", "auth", "refused" or "tls", for UIs that
    // word the problem themselves.
    std::string last_error_code() const;
    const mqtt::Topics& topics() const { return topics_; }
    const Settings& settings() const { return settings_; }

    // Queues a message (QoS 1) without blocking. Safe from any task. Dropped while
    // disconnected: retained state is published again on every connect.
    void publish(const std::string& topic, const std::string& payload, bool retain);

    // Called on the MQTT task.
    std::function<void(bool connected)> on_connection;
    std::function<void(mqtt::Inbound what, std::string payload)> on_command;

private:
    static void on_event(void* self, esp_event_base_t base, std::int32_t id, void* data);
    void handle(esp_mqtt_event_handle_t event);

    Settings settings_;
    mqtt::Topics topics_{""};
    std::string uri_;
    std::string status_topic_;
    esp_mqtt_client_handle_t client_ = nullptr;
    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
    std::string last_error_;
    std::string last_error_code_;
};

}  // namespace tapwave::net
