Migration notes: moving from PubSubClient -> AsyncMqttClient

Goal
----
Provide an async, non-blocking MQTT client with better TLS and reconnection semantics.

Quick steps
-----------
1. Add `AsyncMqttClient` library to `platformio.ini` under `lib_deps` when ready.
2. Add a build flag `-DASYNC_MQTT` to selectively compile the async implementation.
3. Implement an adapter that exposes the minimal API currently used by the codebase:
   - `setServer(host, port)`
   - `connect(clientId, username, password)` (or `connect()` with callbacks)
   - `disconnect()`
   - `publish(topic, payload)`
   - `subscribe(topic)`
   - `connected()`
   - `setCallback()` (message handler)

Notes
-----
- `AsyncMqttClient` uses event/callback based connection handling; code that currently relies on `client.loop()` must be adapted.
- TLS handling differs: prefer using `WiFiClientSecure` or the platform's TLS stack with `AsyncMqttClient` where possible.
- Test thoroughly: connection limits and server behavior (e.g., Bambu printer limits) must be considered.

Example snippet (migration adapter sketch):

```cpp
#ifdef ASYNC_MQTT
#include <AsyncMqttClient.h>
class MqttAdapter {
  AsyncMqttClient mqtt;
  void onMessage(char* topic, uint8_t* payload, unsigned int len) { /*...*/ }
  // implement connect/disconnect/publish/subscribe wrappers
};
#endif
```

When to switch
--------------
- If you continue to see TLS disconnects, race conditions, or high CPU due to blocking reconnect loops.
- If you need better throughput or reliable reconnects during WiFi instability.
