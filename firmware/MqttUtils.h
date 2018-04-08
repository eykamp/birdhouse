#ifndef MQTT_UTILS_H
#define MQTT_UTILS_H

#include <PubSubClient.h>       // For MQTT


WiFiClient wfclient;
PubSubClient pubSubClient(wfclient);


bool mqttLoop() {
  return pubSubClient.loop();
}


const char *getSubPubStatusName(int status) {
  return
    status == MQTT_CONNECTION_TIMEOUT      ? "CONNECTION_TIMEOUT" :       // The server didn't respond within the keepalive time
    status == MQTT_CONNECTION_LOST         ? "CONNECTION_LOST" :          // The network connection was broken
    status == MQTT_CONNECT_FAILED          ? "CONNECT_FAILED" :           // The network connection failed
    status == MQTT_DISCONNECTED            ? "DISCONNECTED" :             // The client is disconnected cleanly
    status == MQTT_CONNECTED               ? "CONNECTED" :                // The cient is connected
    status == MQTT_CONNECT_BAD_PROTOCOL    ? "CONNECT_BAD_PROTOCOL" :     // The server doesn't support the requested version of MQTT
    status == MQTT_CONNECT_BAD_CLIENT_ID   ? "CONNECT_BAD_CLIENT_ID" :    // The server rejected the client identifier
    status == MQTT_CONNECT_UNAVAILABLE     ? "CONNECT_UNAVAILABLE" :      // The server was unable to accept the connection
    status == MQTT_CONNECT_BAD_CREDENTIALS ? "CONNECT_BAD_CREDENTIALS" :  // The username/password were rejected
    status == MQTT_CONNECT_UNAUTHORIZED    ? "CONNECT_UNAUTHORIZED" :     // The client was not authorized to connect
                                             "UNKNOWN";
}


auto mqttState() {
  return pubSubClient.state();
}


const char *getMqttStatus() {
  return getSubPubStatusName(mqttState());
}


void mqttSetServer(IPAddress &ip, uint16_t port) {
  pubSubClient.setServer(ip, port);
}


bool mqttConnect(const char *id, const char *user, const char *pass) {
  return pubSubClient.connect(id, user, pass);
}


bool mqttConnected() {
  return pubSubClient.connected();
}


void mqttDisconnect() {
  pubSubClient.disconnect();
}


bool mqttSubscribe(const char* topic) {
  return pubSubClient.subscribe(topic);
}


void mqttSetCallback(MQTT_CALLBACK_SIGNATURE) {
  pubSubClient.setCallback(callback);
}


bool mqttPublish(const char* topic, const char* payload) {
  return pubSubClient.publish(topic, payload, false);
}


bool mqttPublishAttribute(const JsonObject &jsonObj) {
  String json;
  jsonObj.printTo(json);

  return mqttPublish("v1/devices/me/attributes", json.c_str());
}


bool mqttPublishAttribute(const String &payload) {
  return mqttPublish("v1/devices/me/attributes", payload.c_str());
}


bool mqttPublishTelemetry(const String &payload) {
  return mqttPublish("v1/devices/me/telemetry", payload.c_str());
}


#endif