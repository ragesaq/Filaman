#pragma once

// Adapter scaffold to allow switching to AsyncMqttClient with -DASYNC_MQTT
#ifdef ASYNC_MQTT

#include <AsyncMqttClient.h>
#include <WiFiClientSecure.h>
#include <vector>
#include <string>
#include <functional>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SSLClient.h>
#include <cstring>

using MqttMessageCallback = std::function<void(char*, uint8_t*, unsigned int)>;

class MqttAdapter {
public:
    MqttAdapter();
    void setServer(const IPAddress& host, uint16_t port);
    void setServer(const char* host, uint16_t port);
    void setSocketTimeout(uint16_t secs);
    // TLS configuration
    void setInsecure();
    void setCACert(const char* pem);
    bool isInsecure() const { return insecure_; }
    bool setBufferSize(size_t size);
    void setCallback(void (*cb)(char*, uint8_t*, unsigned int));
    bool connect(const char* clientId, const char* user = nullptr, const char* pass = nullptr);
    void disconnect();
    bool connected();
    void loop();
    bool publish(const char* topic, const char* payload);
    void subscribe(const char* topic);
    int state();
    void setWill(const char* topic, uint8_t qos, bool retain, const char* payload);

private:
    AsyncMqttClient mqtt_;
    WiFiClientSecure secureClient_;
    bool insecure_ = false;
    bool connected_ = false;
    void (*cb_)(char*, uint8_t*, unsigned int) = nullptr;
    std::vector<std::string> subscriptions_;
    std::vector<std::pair<std::string,std::string>> publish_queue_;
    size_t buffer_size_ = 1024;
    size_t max_queue_size_ = 32;
    void flushQueue();
    // Blocking SSL fallback
    WiFiClient* esp_client_ptr_ = nullptr;
    SSLClient* ssl_client_ptr_ = nullptr;
    PubSubClient* blocking_client_ptr_ = nullptr;
    bool use_blocking_ssl_ = false;
    String server_host_;
    uint16_t server_port_ = 0;
    // Will parameters for blocking fallback (PubSubClient doesn't expose setWill)
    std::string will_topic_;
    std::string will_payload_;
    uint8_t will_qos_ = 0;
    bool will_retain_ = false;
    bool has_will_ = false;
};

extern MqttAdapter client; // mimic existing `client` symbol

// (CompatClient removed) Code should use `MqttAdapter client` APIs directly

#endif // ASYNC_MQTT
