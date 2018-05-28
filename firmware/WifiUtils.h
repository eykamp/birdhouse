#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Dns.h>


class WifiUtils {


private:

U32 wifiConnectCooldownTimer = 0;
U32 wifiConnectStartTime = 0;
U32 connections = 0;                    // Keep track of how many times we've connected to wifi.  Possibly a measurement of stability?
U32 lastConnections = 0;

bool needToReconnectToWifi = false;
bool needToConnect = false;
bool isConnectedToWifi = false;
bool wasConnectedToWiFi = false;

bool isConnectingToWifi = false;        // True while a connection is in process
bool changedWifiCredentials = false;    // Track if we've changed wifi connection params during command mode


std::function<void()> onConnectedToWifiCallback;
std::function<void()> onConnectedToWifiFailedCallback;
std::function<void()> onConnectedToWifiTimedOutCallback;
std::function<void()> onDisconnectedFromWifiCallback;

wl_status_t wifiStatus     = WL_DISCONNECTED;
wl_status_t prevWifiStatus = WL_DISCONNECTED;

public:

WifiUtils() {
  onConnectedToWifiCallback = NULL;
  onConnectedToWifiFailedCallback = NULL;
  onConnectedToWifiTimedOutCallback = NULL;
  onDisconnectedFromWifiCallback = NULL;
}


void begin() {
  WiFi.mode(WIFI_AP_STA);  

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);   // Disabling this allows us to connect via wifi AP without issue

  WiFi.begin();
}


void setOnConnectedToWifiCallback(std::function<void()> callback) {
  onConnectedToWifiCallback = callback;
}


void setOnConnectedToWifiFailedCallback(std::function<void()> callback) {
  onConnectedToWifiFailedCallback = callback;
}


void setOnConnectedToWifiTimedOutCallback(std::function<void()> callback) {
  onConnectedToWifiTimedOutCallback = callback;
}


void setOnDisconnectedFromWifiCallback(std::function<void()> callback) {
  onDisconnectedFromWifiCallback = callback;
}


// Called from main firmware loop
void loop() {

  // We want to control this status to ensure that all status changes flow through here; this status will be cached and used elsewhere
  wifiStatus = WiFi.status();
  isConnectedToWifi = (wifiStatus == WL_CONNECTED);


  // There was a change in status -- either we just connected or disconnected
  if(isConnectedToWifi != wasConnectedToWiFi) {
    prevWifiStatus = wifiStatus;
    wasConnectedToWiFi = isConnectedToWifi;

    if(wifiStatus == WL_CONNECTED) {      // We just connected
      isConnectingToWifi = false;
      connections++;

      if(onConnectedToWifiCallback) 
        onConnectedToWifiCallback();
    }

    else {                                // We just disconnected
      isConnectingToWifi = false;
      wifiConnectCooldownTimer = millis();

      if(onDisconnectedFromWifiCallback)
        onDisconnectedFromWifiCallback();
    }
  }

  else if(isConnectingToWifi) {
    if(millis() - wifiConnectStartTime > 15 * SECONDS) {
      // I'm tired of waiting!!!
      wifiConnectCooldownTimer = millis();
      isConnectingToWifi = false;

      if(onConnectedToWifiTimedOutCallback)
        onConnectedToWifiTimedOutCallback();
    }
  }

  // There was no change in status
  else {
    if(wifiStatus == WL_CONNECTED) {    // Already connected
      if(!changedWifiCredentials)       // Credentials didn't change, so nothing more to do
        return;

      // Credentials DID change, so we need to disconnect
      WiFi.disconnect();
      isConnectingToWifi = false;
      changedWifiCredentials = false;
      lastConnections = connections;    // Suppress call to onDisconnectedToWifi below
      delay(500);
      wifiConnectCooldownTimer = 0;
    }
    // If we're disconnected, let's reconnect!
    else if(wifiStatus != WL_CONNECTED) {
      if(isConnectingToWifi)                                  //  We've already initated a connection
        return;

      if(millis() - wifiConnectCooldownTimer < 5 * SECONDS)   // Don't do anything during the cooldown period
        return;

      // Reconnect!
      wl_status_t stat = WiFi.begin(Eeprom.getWifiSsid(), Eeprom.getWifiPassword());    // Status is usually WL_DISCONNECTED when returning from this function

      isConnectingToWifi = true;
      wifiConnectStartTime = millis();
    }
  }


  // else if(status == WL_CONNECT_FAILED) {       // PROBLEM!  (possible, never seen this in practice)
  //   isConnectingToWifi = false;
  //   wifiConnectCooldownTimer = millis();

  //   if(onConnectedToWifiFailedCallback)
  //     onConnectedToWifiFailedCallback();
  // }
  // else {                                       // OK... we'll check back later
  //   isConnectingToWifi = true;
  //   wifiConnectStartTime = millis();    
  // }
}



U32 getConnectionCount() {
  return connections;
}


// Called from setup
void setupLocalAccessPoint(const char *ssid, const char *password, const char *localAccessPointAddress)
{
  // static const char *localGatewayAddress = "192.168.1.2";

  // Resolve addresses
  IPAddress ip;
  WiFi.hostByName(localAccessPointAddress, ip);
  // WiFi.hostByName(localGatewayAddress, gateway);

  IPAddress gateway(192,168,1,2);
  IPAddress subnetMask(255,255,255,0);


  bool ok = WiFi.softAPConfig(ip, gateway, subnetMask) && WiFi.softAP(ssid, password);


  const char *dnsName = "birdhouse";      // Connect with birdhouse.local

  if (MDNS.begin(dnsName)) {              // Start the mDNS responder for birdhouse.local
    //xx Serial.printf("mDNS responder started: %s.local\n", dnsName);
  }
  else {
    //xx Serial.println("Error setting up MDNS responder!");
  }
}



void setChangedWifiCredentials() {
  changedWifiCredentials = true;
}


bool isConnected() {
  return wifiStatus == WL_CONNECTED;
}


const char *getWifiStatusName() {
  return getWifiStatusName(wifiStatus);
}


String scanVisibleNetworks() {

  WiFi.scanNetworks(false, true);    // Include hidden access points

  U32 scanStartTime = millis();

  // Wait for scan to complete with max 30 seconds
  static const int MAX_SCAN_TIME = 30 * SECONDS;

  while(!WiFi.scanComplete()) { 
    if(millis() - scanStartTime > MAX_SCAN_TIME) {
      return "";
    }
  }

  return getScanResults();
}


String getScanResults() {
  int networksFound = WiFi.scanComplete();

  // This is the format the Google Location services uses.  We'll create a list of these packets here so they can be passed through by the microservice
  // {
  //   "macAddress": "00:25:9c:cf:1c:ac",
  //   "signalStrength": -43,
  //   "age": 0,
  //   "channel": 11,
  //   "signalToNoiseRatio": 0
  // }

  String json = "["; 

  for (int i = 0; i < networksFound; i++) {
    json += "{\\\"macAddress\\\":\\\"" + WiFi.BSSIDstr(i) + "\\\",\\\"signalStrength\\\":" + String(WiFi.RSSI(i)) + 
                ", \\\"age\\\": 0, \\\"channel\\\":" + String(WiFi.channel(i)) + ",\\\"signalToNoiseRatio\\\": 0 }";
    if(i < networksFound - 1)
      json += ",";
  }
  json += "]";

  return json;
}



const char *getWifiStatusName(wl_status_t status) {
  return
    status == WL_NO_SHIELD        ? "NO_SHIELD" :
    status == WL_IDLE_STATUS      ? "IDLE_STATUS" :
    status == WL_NO_SSID_AVAIL    ? "NO_SSID_AVAIL" :
    status == WL_SCAN_COMPLETED   ? "SCAN_COMPLETED" :
    status == WL_CONNECTED        ? "CONNECTED" :
    status == WL_CONNECT_FAILED   ? "CONNECT_FAILED" :
    status == WL_CONNECTION_LOST  ? "CONNECTION_LOST" :
    status == WL_DISCONNECTED     ? "DISCONNECTED" :
                                    "UNKNOWN";
}




};

WifiUtils wifiUtils;


#endif