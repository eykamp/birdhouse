#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Dns.h>


class WifiUtils {


private:

U32 wifiConnectCooldownTimer = 0;
U32 wifiConnectStartTime= 0;

bool needToReconnectToWifi = false;
bool needToConnect = false;

bool isConnectingToWifi = false;        // True while a connection is in process
bool changedWifiCredentials = false;    // Track if we've changed wifi connection params during command mode


std::function<void()> onConnectedToWifiCallback;
std::function<void()> onConnectedToWifiFailedCallback;
std::function<void()> onConnectedToWifiTimedOutCallback;


public:

WifiUtils() {
  onConnectedToWifiCallback = NULL;
  onConnectedToWifiFailedCallback = NULL;
  onConnectedToWifiTimedOutCallback = NULL;
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



// Called from startup and loop
void loop() {

  // If our connection fails, let's take a brief break before trying again
  // Don't do anything during the cooldown period
  static const U32 WIFI_CONNECT_COOLDOWN_PERIOD = 5 * SECONDS;

  if(millis() - wifiConnectCooldownTimer < WIFI_CONNECT_COOLDOWN_PERIOD)
    return;

  // Handle a connection that is already underway:
  if(isConnectingToWifi) {
    if(WiFi.status() == WL_CONNECTED) {   // We just connected!  :-)

      if(onConnectedToWifiCallback)
        onConnectedToWifiCallback();

      isConnectingToWifi = false;
      return;
    }


    // Still working at it  :-(
    if(millis() - wifiConnectStartTime > 20 * SECONDS) {
      isConnectingToWifi = false;
      wifiConnectCooldownTimer = millis();

      if(onConnectedToWifiTimedOutCallback)
        onConnectedToWifiTimedOutCallback();
    }

    return;
  }

  
  if(WiFi.status() == WL_CONNECTED) {   // Already connected
    if(!changedWifiCredentials)         // Don't disconnect, so nothing to do
      return;

    // Our credentials changed, so we need to disconnect
    WiFi.disconnect();
    isConnectingToWifi = false;
    changedWifiCredentials = false;
    delay(500);
  }


  // If we get here, we aren't connected, and we're ready to start things up

  // Clear some error statuses
  int status = WiFi.begin(Eeprom.getWifiSsid(), Eeprom.getWifiPassword());

  if(status != WL_CONNECT_FAILED) {   // OK
    isConnectingToWifi = true;
    wifiConnectStartTime = millis();    
  }
  else {                              // PROBLEM!
    wifiConnectCooldownTimer = millis();

    if(onConnectedToWifiFailedCallback)
      onConnectedToWifiFailedCallback();
  }
}


// Called from setup
void setupLocalAccessPoint(const char *ssid, const char *password, const char *localAccessPointAddress)
{
  static const char *localGatewayAddress = "192.168.1.2";


  // Resolve addresses
  IPAddress ip, gateway;
  WiFi.hostByName(localAccessPointAddress, ip);
  WiFi.hostByName(localGatewayAddress, gateway);

  IPAddress subnetMask(255,255,255,0);
 
  bool ok = WiFi.softAPConfig(ip, gateway, subnetMask) && WiFi.softAP(ssid, password); //, wifiChannel, false);  // name, pw, channel, hidden

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
  return WiFi.status() == WL_CONNECTED;
}


const char *getWifiStatusName() {
  return getWifiStatusName(WiFi.status());
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