#ifndef MQTT_UTILS_H
#define MQTT_UTILS_H

#include <PubSubClient.h>       // For MQTT


WiFiClient wfclient;
PubSubClient pubSubClient(wfclient);


class MqttUtils {

public:

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


int mqttState() {
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


void publishSampleDuration(U16 sampleDuration) {
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["sampleDuration"] = sampleDuration;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);  
}


void publishCredentials(const char *ssid, const char *password, const char *ipAddr, const char *wifiSsid, const char *wifiPassword, const IPAddress &lanIpAddress) {
  String json = String("{") + 
    "\"localCredentials\": \"{" +  
      "\\\"ssid\\\":"       + "\\\"" + String(ssid) + "\\\"," +
      "\\\"password\\\":"   + "\\\"" + String(password) + "\\\"," +
      "\\\"ipAddress\\\":"  + "\\\"" + String(ipAddr) + "\\\"" +
    "}\"," +

    "\"lanCredentials\": \"{" +    
      "\\\"ssid\\\":"       + "\\\"" + String(wifiSsid) + "\\\"," +
      "\\\"password\\\":"   + "\\\"" + String(wifiPassword) + "\\\"," +
      "\\\"ipAddress\\\":"  + "\\\"" + lanIpAddress.toString() + "\\\"" +
    "}\"" +
  "}";

  mqttPublishAttribute(json);
}


void publishTempSensorNameAndSoftwareVersion(const char *tempSensorName, const char *firmwareVersion, const String &macAddr) {
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["temperatureSensor"] = tempSensorName;
  root["firmwareVersion"] = firmwareVersion;
  root["macAddress"] = macAddr;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);
}


bool publishDeviceData(U32 uptime, U32 freeHeap) {

    String json = String("{") +
      "\"uptime\":"              + String(uptime) + "," + 
      "\"freeHeap\":"            + String(freeHeap) + "}"; 

    return mqttPublishTelemetry(json);

}

void reportPlantowerDetectNondetect(bool sensorFound) {
  mqttPublishAttribute(String("{\"plantowerSensorDetected\":") + (sensorFound ? "True" : "False") + "}");
}


void publishOtaStatusMessage(const String &msg) {
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["otaUpdate"] = msg;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);  
}


void publishStatusMessage(const String &msg) {
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["status"] = msg;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);  
}


void publishResetReason(const String &reason) {
  mqttPublishTelemetry(String("{\"resetReason\":\"") + reason + "\"}");
}


void publishDnsResult(const String &reason) {
  mqttPublishTelemetry(String("{\"dnsResult\":\"") + reason + "\"}");
}


void publishWifiScanResults(const String &results) {
  mqttPublishAttribute(String("{\"visibleHotspots\":\"") + results + "\"}");
}


String quote(const String &str) {
  String quoted = str;

  quoted.replace("\"", "\\\"");

  return quoted;
}


void publishCalibrationFactors(const String &calibrationFactors) {
  mqttPublishAttribute(String("{\"calibrationFactors\":\"") + quote(calibrationFactors) + "\"}");
}


void publishPmData(F64 pm1, F64 pm25, F64 pm10, F64 rawPm1, F64 rawPm25, F64 rawPm10, U16 sampleCount) {

  String json = String("{") +
    "\"plantowerPM1conc\":"     + String(pm1)  + "," + 
    "\"plantowerPM25conc\":"    + String(pm25) + "," + 
    "\"plantowerPM10conc\":"    + String(pm10) + "," +
    "\"plantowerPM1concRaw\":"  + String(rawPm1)  + "," + 
    "\"plantowerPM25concRaw\":" + String(rawPm25) + "," + 
    "\"plantowerPM10concRaw\":" + String(rawPm10) + "," +
    "\"plantowerSampleCount\":" + String(sampleCount) + "}";

  mqttPublishTelemetry(json);

}


void publishWeatherData(F32 rawTemp, F32 smoothedTemp, F32 humidity, F32 pressure, bool reportHumidity) {
  String json = "{\"temperature\":" + String(rawTemp) + ",\"pressure\":" + String(pressure) + ",\"temperature_smoothed\":" + String(smoothedTemp);

  if(reportHumidity)
    json += ",\"humidity\":" + String(humidity);
  json += "}";

  mqttPublishTelemetry(json);
}


bool containsKey(const JsonObject &obj, const char *key) {
  for(const JsonPair &pair : obj) {
    if(strcmp(pair.key, key) == 0)
      return true;
  }

  return false;
}

};


MqttUtils mqtt;


#endif