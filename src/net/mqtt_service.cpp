#include "net/mqtt_service.hpp"

#include <esp_crt_bundle.h>
#include <esp_log.h>

namespace tapwave::net {

namespace {

constexpr const char* kTag = "mqtt";

}  // namespace

void MqttService::start(const Settings& settings) {
    stop();
    settings_ = settings;
    topics_ = mqtt::Topics(settings_.topic_prefix);
    status_topic_ = topics_.status();

    esp_mqtt_client_config_t config{};
    config.broker.address.hostname = settings_.host.c_str();
    config.broker.address.port = static_cast<std::uint32_t>(settings_.port);
    config.broker.address.transport =
        settings_.tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    if (settings_.tls) {
        config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    config.credentials.client_id = settings_.client_id.c_str();
    if (!settings_.username.empty()) {
        config.credentials.username = settings_.username.c_str();
        config.credentials.authentication.password = settings_.password.c_str();
    }
    config.session.keepalive = 30;
    config.session.last_will.topic = status_topic_.c_str();
    config.session.last_will.msg = "offline";
    config.session.last_will.msg_len = 7;
    config.session.last_will.qos = 1;
    config.session.last_will.retain = 1;
    config.network.reconnect_timeout_ms = 10000;
    config.buffer.size = 2048;
    config.buffer.out_size = 2048;
    config.task.stack_size = 6144;

    client_ = esp_mqtt_client_init(&config);
    if (!client_) {
        std::scoped_lock lock(mutex_);
        last_error_ = "the MQTT client could not be created";
        return;
    }
    esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttService::on_event, this);
    esp_mqtt_client_start(client_);
    ESP_LOGI(kTag, "connecting to %s:%d", settings_.host.c_str(), settings_.port);
}

void MqttService::stop() {
    if (!client_) {
        return;
    }
    if (connected_) {
        esp_mqtt_client_publish(client_, status_topic_.c_str(), "offline", 7, 1, 1);
    }
    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    connected_ = false;
}

std::string MqttService::last_error() const {
    std::scoped_lock lock(mutex_);
    return last_error_;
}

void MqttService::publish(const std::string& topic, const std::string& payload, bool retain) {
    if (!client_ || !connected_) {
        return;
    }
    esp_mqtt_client_enqueue(client_, topic.c_str(), payload.data(),
                            static_cast<int>(payload.size()), 1, retain ? 1 : 0, true);
}

void MqttService::on_event(void* self, esp_event_base_t, std::int32_t, void* data) {
    static_cast<MqttService*>(self)->handle(static_cast<esp_mqtt_event_handle_t>(data));
}

void MqttService::handle(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: {
            connected_ = true;
            {
                std::scoped_lock lock(mutex_);
                last_error_.clear();
            }
            esp_mqtt_client_publish(client_, status_topic_.c_str(), "online", 6, 1, 1);
            for (const std::string& topic : topics_.subscriptions()) {
                esp_mqtt_client_subscribe_single(client_, topic.c_str(), 1);
            }
            const std::string ha_status = settings_.discovery_prefix + "/status";
            esp_mqtt_client_subscribe_single(client_, ha_status.c_str(), 1);
            ESP_LOGI(kTag, "connected");
            if (on_connection) {
                on_connection(true);
            }
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            if (connected_.exchange(false)) {
                ESP_LOGW(kTag, "disconnected");
                if (on_connection) {
                    on_connection(false);
                }
            }
            break;
        case MQTT_EVENT_DATA: {
            // Commands are small; a message split over several events is not one of ours.
            if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
                break;
            }
            const std::string topic(event->topic, static_cast<std::size_t>(event->topic_len));
            const auto what = mqtt::route(topics_, settings_.discovery_prefix, topic);
            if (what != mqtt::Inbound::Unknown && on_command) {
                on_command(what,
                           std::string(event->data, static_cast<std::size_t>(event->data_len)));
            }
            break;
        }
        case MQTT_EVENT_ERROR: {
            std::string reason = "the broker could not be reached";
            if (event->error_handle) {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    reason = event->error_handle->connect_return_code ==
                                     MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED
                                 ? "the broker refused the user name or password"
                                 : "the broker refused the connection";
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT &&
                           event->error_handle->esp_tls_last_esp_err != ESP_OK) {
                    reason = "the TLS connection to the broker failed";
                }
            }
            ESP_LOGW(kTag, "%s", reason.c_str());
            std::scoped_lock lock(mutex_);
            last_error_ = std::move(reason);
            break;
        }
        default:
            break;
    }
}

}  // namespace tapwave::net
