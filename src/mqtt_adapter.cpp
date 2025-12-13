#ifdef ASYNC_MQTT
#include "mqtt_adapter.h"
#include <Arduino.h>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_task_wdt.h>
#endif

#include <WiFi.h>
#include <PubSubClient.h>
#include <SSLClient.h>
#include <cstring>

MqttAdapter client;

MqttAdapter::MqttAdapter() : secureClient_() {
    // Note: AsyncMqttClient handles TLS internally when built with SSL support.
    // We do not pass a WiFiClientSecure to it here because this AsyncMqttClient
    // revision does not expose a setClient() API. Use setInsecure()/setCACert()
    // to configure TLS behavior via AsyncMqttClient where available.

    mqtt_.onConnect([this](bool sessionPresent){
        this->connected_ = true;
        Serial.println("MqttAdapter: onConnect()");
        Serial.printf("MqttAdapter: freeHeap=%u, sessionPresent=%d\n", ESP.getFreeHeap(), (int)sessionPresent);
        // flush any queued publishes
        this->flushQueue();
        // resubscribe
        for (auto &t : subscriptions_) {
            mqtt_.subscribe(t.c_str(), 0);
        }
    });

    mqtt_.onDisconnect([this](AsyncMqttClientDisconnectReason reason){
        this->connected_ = false;
        Serial.printf("MqttAdapter: onDisconnect() reason=%d, connected_flag=%d\n", (int)reason, (int)this->connected_);
    });

    mqtt_.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties props, size_t len, size_t index, size_t total) {
        if (this->cb_) {
            uint8_t* buf = (uint8_t*)payload;
            this->cb_(topic, buf, (unsigned int)len);
        }
    });
}

void MqttAdapter::setServer(const IPAddress& host, uint16_t port) {
    // store host/port for possible blocking fallback
    server_host_ = host.toString();
    server_port_ = port;
    mqtt_.setServer(host, port);
}

void MqttAdapter::setServer(const char* host, uint16_t port) {
    server_host_ = String(host);
    server_port_ = port;
    mqtt_.setServer(host, port);
}

void MqttAdapter::setSocketTimeout(uint16_t secs) {
    // Attempt to set a timeout on the underlying secure client (ms)
    // WiFiClientSecure uses seconds in setTimeout on some platforms, convert conservatively
    secureClient_.setTimeout((int)secs * 1000);
}

void MqttAdapter::setInsecure() {
    // Mark adapter as 'insecure' mode: caller intends to disable cert checks.
    // AsyncMqttClient does not expose a direct API to disable verification,
    // so we record the intent and avoid blocking indefinitely during connect.
    // Configure the local secure client to skip cert verification.
#ifdef ARDUINO_ARCH_ESP32
    secureClient_.setInsecure();
#endif
#if ASYNC_TCP_SSL_ENABLED
    // Ask AsyncMqttClient to use secure mode (it will create TLS sockets internally).
    mqtt_.setSecure(true);
#endif
    insecure_ = true;
    Serial.println("MqttAdapter: setInsecure() called - TLS verification disabled on secure client (if supported)");
}

void MqttAdapter::setCACert(const char* pem) {
    // AsyncMqttClient doesn't accept PEM CA directly; store for diagnostics.
    (void)pem;
#if ASYNC_TCP_SSL_ENABLED
    mqtt_.setSecure(true);
#endif
}

bool MqttAdapter::setBufferSize(size_t size) {
    // AsyncMqttClient does not provide a public API to grow internal buffers here.
    // Store requested size for diagnostics and accept the request.
    buffer_size_ = size;
    return true;
}

void MqttAdapter::setCallback(void (*cb)(char*, uint8_t*, unsigned int)) {
    cb_ = cb;
}

void MqttAdapter::flushQueue() {
    if (!connected_) return;
    Serial.printf("MqttAdapter: flushing publish queue (%u messages)\n", (unsigned)publish_queue_.size());
    for (auto &p : publish_queue_) {
        uint16_t msgId = mqtt_.publish(p.first.c_str(), 0, false, p.second.c_str());
        Serial.printf("MqttAdapter: flushed topic=%s msgid=%u\n", p.first.c_str(), (unsigned)msgId);
    }
    publish_queue_.clear();
}

bool MqttAdapter::connect(const char* clientId, const char* user, const char* pass) {
    if (user) mqtt_.setCredentials(user, pass ? pass : "");
    if (clientId) mqtt_.setClientId(clientId);
    // Determine whether AsyncTCP/AsyncMqttClient was built with SSL support
    int async_ssl = 0;
#ifdef ASYNC_TCP_SSL_ENABLED
    async_ssl = 1;
#endif
    Serial.printf("MqttAdapter: connect() clientId=%s user=%s insecure_flag=%d async_ssl_enabled=%d\n",
                  clientId ? clientId : "(null)", user ? user : "(null)", (int)insecure_, async_ssl);
    bool want_tls_port = (server_port_ == 8883);
    // CRITICAL FIX: Always prefer blocking PubSubClient for Bambu MQTT reliability
    // AsyncMqttClient has stability issues with Bambu's message handling
    bool use_blocking = true;

    if (use_blocking) {
        Serial.println("MqttAdapter: using blocking SSL PubSubClient fallback (TLS via SSLClient)");
        use_blocking_ssl_ = true;
        // create clients if not present
        if (!esp_client_ptr_) esp_client_ptr_ = new WiFiClient();
        if (!ssl_client_ptr_) ssl_client_ptr_ = new SSLClient(esp_client_ptr_);
        if (!blocking_client_ptr_) blocking_client_ptr_ = new PubSubClient(*ssl_client_ptr_);
        // configure SSL client
        if (insecure_) {
#ifdef ARDUINO_ARCH_ESP32
            ssl_client_ptr_->setInsecure();
#endif
        }
        blocking_client_ptr_->setServer(server_host_.c_str(), server_port_);
        // set callback wrapper
        blocking_client_ptr_->setCallback([this](char* topic, uint8_t* payload, unsigned int length){
            if (this->cb_) this->cb_(topic, payload, length);
        });
        bool ok;
        if (has_will_) {
            if (user && pass) ok = blocking_client_ptr_->connect(clientId, user, pass,
                                                              will_topic_.c_str(), will_qos_, will_retain_, will_payload_.c_str());
            else ok = blocking_client_ptr_->connect(clientId, will_topic_.c_str(), will_qos_, will_retain_, will_payload_.c_str());
        } else {
            if (user && pass) ok = blocking_client_ptr_->connect(clientId, user, pass);
            else ok = blocking_client_ptr_->connect(clientId);
        }
        connected_ = ok;
        if (connected_) {
            Serial.println("MqttAdapter: blocking client connected");
            // resubscribe
            for (auto &t : subscriptions_) {
                blocking_client_ptr_->subscribe(t.c_str());
            }
            // flush queued publishes
            this->flushQueue();
        } else {
            Serial.println("MqttAdapter: blocking client connect failed");
        }
        return connected_;
    }

    // Default: use async mqtt
    mqtt_.connect();
    unsigned long start = millis();
    while (!connected_ && (millis() - start) < 10000) {
        // Keep watchdog happy while waiting for async connect
        esp_task_wdt_reset();
        // Also print intermediate state to help diagnose hangs
        if (((millis() - start) % 2000) < 60) {
            Serial.printf("MqttAdapter: waiting for onConnect(), connected_flag=%d elapsed=%lums\n", (int)connected_, millis()-start);
        }
        delay(50);
    }
    if (!connected_) {
        Serial.printf("MqttAdapter: connect() failed after %lums, connected_flag=%d\n", millis()-start, (int)connected_);
    }
    return connected_;
}

void MqttAdapter::disconnect() {
    if (use_blocking_ssl_ && blocking_client_ptr_) {
        blocking_client_ptr_->disconnect();
        connected_ = false;
        return;
    }
    mqtt_.disconnect();
}

bool MqttAdapter::connected() {
    return connected_;
}

void MqttAdapter::loop() {
    if (use_blocking_ssl_ && blocking_client_ptr_) {
        // PubSubClient requires explicit loop() call to:
        // 1. Maintain connection with keepalive pings
        // 2. Receive incoming messages
        // 3. Handle reconnection logic
        blocking_client_ptr_->loop();
    } 
    // Note: AsyncMqttClient manages its own event loop internally via FreeRTOS tasks
    // and does not require explicit loop() calls. Message delivery happens via callbacks
    // registered in the onMessage() handler during construction.
}

bool MqttAdapter::publish(const char* topic, const char* payload) {
    if (!connected_) {
        // queue for later (bounded by max_queue_size_)
        if (publish_queue_.size() < max_queue_size_) {
            publish_queue_.emplace_back(std::string(topic), std::string(payload));
            Serial.printf("MqttAdapter: queued publish topic=%s queue_sz=%u\n", topic, (unsigned)publish_queue_.size());
            return true;
        }
        return false;
    }
    if (use_blocking_ssl_ && blocking_client_ptr_) {
        bool ok = blocking_client_ptr_->publish(topic, payload);
        Serial.printf("MqttAdapter: blocking publish topic=%s ok=%d\n", topic, (int)ok);
        return ok;
    }
    uint16_t msgId = mqtt_.publish(topic, 0, false, payload);
    Serial.printf("MqttAdapter: publish topic=%s msgid=%u\n", topic, (unsigned)msgId);
    return true;
}

void MqttAdapter::subscribe(const char* topic) {
    subscriptions_.push_back(std::string(topic));
    Serial.printf("MqttAdapter: subscribe requested topic=%s connected=%d\n", topic, (int)connected_);
    if (connected_) {
        if (use_blocking_ssl_ && blocking_client_ptr_) {
            bool ok = blocking_client_ptr_->subscribe(topic);
            Serial.printf("MqttAdapter: blocking subscribe topic=%s ok=%d\n", topic, (int)ok);
        } else {
            uint16_t subId = mqtt_.subscribe(topic, 0);
            Serial.printf("MqttAdapter: subscribe sent topic=%s subid=%u\n", topic, (unsigned)subId);
        }
    }
}

int MqttAdapter::state() {
    return connected_ ? 0 : -1;
}

void MqttAdapter::setWill(const char* topic, uint8_t qos, bool retain, const char* payload) {
    // Store will parameters for blocking fallback (PubSubClient does not provide setWill())
    will_topic_ = topic ? topic : std::string();
    will_payload_ = payload ? payload : std::string();
    will_qos_ = qos;
    will_retain_ = retain;
    has_will_ = true;
    if (!use_blocking_ssl_) {
        mqtt_.setWill(topic, qos, retain, payload);
    }
}

#endif // ASYNC_MQTT
