#define AREST_NUMBER_VARIABLES 30
#define AREST_NUMBER_FUNCTIONS 20
#define AREST_PARAMS_MODE 1
#define AREST_BUFFER_SIZE 4000

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <Dns.h>
#include <BME280I2C.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_DotStar.h>   // Handle those single LED variants
#include <PMS.h>                // Plantower

// OTA Updates
#include <ESP8266httpUpdate.h>

#include "C:/dev/birdhouse/firmware/Types.h"
#include "C:/dev/birdhouse/firmware/Intervals.h"
#include "C:/dev/birdhouse/firmware/BirdhouseEeprom.h"    // For persisting values in EEPROM
#include "C:/dev/birdhouse/firmware/ESP8266Ping.h"        // For ping, of course
#include "C:/dev/birdhouse/firmware/MqttUtils.h"
#include "C:/dev/birdhouse/firmware/ParameterManager.h"
#include "C:/dev/birdhouse/firmware/LedUtils.h"

using namespace std;

#include "c:/dev/aREST/aREST.h"              // Our REST server for provisioning
// #include "aREST2.h"


#define BME_SCL D5  // SPI (Clock)
#define BME_SDA D6  // SDA (Data) 

// Plantower Sensor pins are hardcoded below; they have to be on the serial pins

// Output LEDs
#define LED_BUILTIN D4
#define LED_GREEN   D0
#define LED_YELLOW  D1
#define LED_RED     D2
///// OR /////
#define LED_DATA_PIN D1
#define LED_CLOCK_PIN D0


ParameterManager paramManager;

//////////////////////
// WiFi Definitions //
//////////////////////


aREST Rest = aREST();

#define PLANTOWER_SERIAL_BAUD_RATE 9600
#define SERIAL_BAUD_RATE 115200

// Plantower config
PMS pms(Serial);
PMS::DATA PmsData;

bool plantowerSensorDetected = false;




// U16 wifiChannel = 11;   // TODO: Delete? EEPROM, 0 = default, 1-13 are valid values


const char *localAccessPointAddress = "192.168.1.1";    // Url a user connected by wifi would use to access the device server
const char *localGatewayAddress = "192.168.1.2";

void connectToWiFi(bool); // Forward declare


U32 lastScanTime = 0;


U32 lastPubSubConnectAttempt = 0;
bool mqttServerConfigured = false;
bool mqttServerLookupError = false;

bool reportedResetReason = false;


void setupPubSubClient() {
  if(mqttServerConfigured || mqttServerLookupError )
    return;

  if(WiFi.status() != WL_CONNECTED)   // No point in doing anything here if we don't have internet access
    return;

  IPAddress serverIp;

  if(WiFi.hostByName(Eeprom.getMqttUrl(), serverIp)) {
    mqtt.mqttSetServer(serverIp, Eeprom.getMqttPort());
    mqttServerConfigured = true;
  } else {
    mqttServerLookupError = true;   // TODO: Try again in a few minutes
  }
}


U32 now_millis;

bool serialSwapped = false;


void loopPubSub() {
  setupPubSubClient();

  if(!mqttServerConfigured)
    return;

  // Ensure constant contact with the mother ship
  if(!mqtt.mqttConnected()) {
    if (now_millis - lastPubSubConnectAttempt > 5 * SECONDS) {
      reconnectToPubSubServer();      // Attempt to reconnect
      lastPubSubConnectAttempt = now_millis;
      return;
    }
  }

  mqtt.mqttLoop();
}


// Gets run when we're not connected to the PubSub client
void reconnectToPubSubServer() {
  if(!mqttServerConfigured)
    return;

  if(WiFi.status() != WL_CONNECTED)   // No point in doing anything here if we don't have internet access
    return;

  // Attempt to connect
  if(mqtt.mqttConnect("Birdhouse", Eeprom.getDeviceToken(), "")) {   // ClientID, username, password
    onConnectedToPubSubServer();
  }
}



void onConnectedToPubSubServer() {
  // Announce ourselves to the server
  mqtt.publishStatusMessage("Initialized");
}



const char *defaultPingTargetHostName = "www.google.com";


const char *getWifiStatus() {
  return getWifiStatusName(WiFi.status());
}

const char *getMqttStatus() {
  return mqtt.getMqttStatus();
}

int setParamsHandler(String params) {     // Can't be const ref because of aREST design
  return paramManager.setParamsHandler(params);
}



const U32 MAX_COMMAND_LENGTH = 128;
String command;     // The command the user is composing during command mode

bool changedWifiCredentials = false;    // Track if we've changed wifi connection params during command mode

void setup() {

  Serial.begin(SERIAL_BAUD_RATE);

  Serial.println("");
  Serial.println("");
  Serial.println("   _____                            __          __ ");
  Serial.println("  / ___/___  ____  _________  _____/ /_  ____  / /_");
  Serial.println("  \\__ \\/ _ \\/ __ \\/ ___/ __ \\/ ___/ __ \\/ __ \\/ __/");
  Serial.println(" ___/ /  __/ / / (__  ) /_/ / /  / /_/ / /_/ / /_  ");
  Serial.println("/____/\\___/_/ /_/____/\\____/_/  /_.___/\\____/\\__/  ");
  Serial.println("");
  Serial.println("");

  intitialConfig();
  Eeprom.begin();

  ledUtils.begin(LED_RED, LED_YELLOW, LED_GREEN, LED_BUILTIN, LED_DATA_PIN, LED_CLOCK_PIN);
  updateLedStyle();
  ledUtils.playStartupSequence();


  Rest.variable("uptime",   &millis);
  Rest.variable("ledStyle", &getLedStyle);
  Rest.variable("sensorsDetected",    &getSensorsDetected,    false);      // false ==> We'll handle quoting ourselves
  Rest.variable("calibrationFactors", &getCalibrationFactors, false);

  Rest.variable("mqttStatus", &getMqttStatus);
  Rest.variable("wifiStatus", &getWifiStatus);
  Rest.variable("mqttServerConfigured", &mqttServerConfigured);
  Rest.variable("mqttServerLookupError", &mqttServerLookupError);


  Rest.variable("wifiScanResults", &getScanResults, false);
  Rest.variable("sampleDuration",  &getSampleDuration);
  Rest.variable("deviceToken",     &getDeviceToken);
  Rest.variable("localSsid",       &getLocalSsid);
  Rest.variable("localPass",       &getLocalPassword);
  Rest.variable("wifiSsid",        &getWifiSsid);
  Rest.variable("wifiPass",        &getWifiPassword);
  Rest.variable("mqttUrl",         &getMqttUrl);
  Rest.variable("mqttPort",        &getMqttPort);
  Rest.variable("serialNumber",    &getBirdhouseNumber);
  Rest.variable("firmwareVersion", &getFirmwareVersion);


  // These all take a single parameter specified on the cmd line
  Rest.function("setparams", setParamsHandler);
  Rest.function("restart",  rebootHandler);
  Rest.function("updateFirmware", updateFirmware);
  Rest.function("leds",     ledsHandler);


  Rest.set_id("brdhse");  // Should be 9 chars or less
  Rest.set_name(Eeprom.getLocalSsid());

  WiFi.mode(WIFI_AP_STA);  
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);


  paramManager.setWifiCredentialsChangedCallback([]() { changedWifiCredentials = true; });
  paramManager.setMqttCredentialsChangedCallback(onMqttPortUpdated);
  paramManager.setLedStyleChangedCallback(updateLedStyle());

  setupPubSubClient();

  setupSensors();
 
  command.reserve(MAX_COMMAND_LENGTH);

  setupLocalAccessPoint(Eeprom.getLocalSsid(), Eeprom.getLocalPassword());
  connectToWiFi(false);
}


void updateLedStyle() {
  ledUtils.setLedStyle(static_cast<ParameterManager::LedStyle>(Eeprom.getLedStyle()));
}


// Pass through functions for aREST variables
const char *getDeviceToken()     { return Eeprom.getDeviceToken();     }
const char *getLocalSsid()       { return Eeprom.getLocalSsid();       }
const char *getLocalPassword()   { return Eeprom.getLocalPassword();   }
const char *getWifiSsid()        { return Eeprom.getWifiSsid();        }
const char *getWifiPassword()    { return Eeprom.getWifiPassword();    }
const char *getMqttUrl()         { return Eeprom.getMqttUrl();         }
const char *getFirmwareVersion() { return "Configuration Firmware";    }
U16 getSampleDuration()          { return Eeprom.getSampleDuration();  }
U16 getMqttPort()                { return Eeprom.getMqttPort();        }
U16 getBirdhouseNumber()         { return Eeprom.getBirdhouseNumber(); }


String getLedStyle() {
  return ParameterManager::getLedStyleName((ParameterManager::LedStyle) Eeprom.getLedStyle());
}


String getSensorsDetected() {
  return String("{\"plantowerSensorDetected\":") + (plantowerSensorDetected ? "true" : "false") + ",\"temperatureSensor\":\"" + getTemperatureSensorName() + "\"}";
}


String getCalibrationFactors() {
  return String("{") + 
    "\"temperatureCalibrationFactor\":" + String(Eeprom.getTemperatureCalibrationFactor()) + "," +
    "\"temperatureCalibrationOffset\":" + String(Eeprom.getTemperatureCalibrationOffset()) + "," +
    "\"humidityCalibrationFactor\":"    + String(Eeprom.getHumidityCalibrationFactor())    + "," +
    "\"humidityCalibrationOffset\":"    + String(Eeprom.getHumidityCalibrationOffset())    + "," +
    "\"pressureCalibrationFactor\":"    + String(Eeprom.getPressureCalibrationFactor())    + "," +
    "\"pressureCalibrationOffset\":"    + String(Eeprom.getPressureCalibrationOffset())    + "," +
    "\"pm10CalibrationFactor\":"        + String(Eeprom.getPM10CalibrationFactor())        + "," +
    "\"pm10CalibrationOffset\":"        + String(Eeprom.getPM10CalibrationOffset())        + "," +
    "\"pm25CalibrationFactor\":"        + String(Eeprom.getPM25CalibrationFactor())        + "," +
    "\"pm25CalibrationOffset\":"        + String(Eeprom.getPM25CalibrationOffset())        + "," +
    "\"pm1CalibrationFactor\":"         + String(Eeprom.getPM1CalibrationFactor())         + "," +
    "\"pm1CalibrationOffset\":"         + String(Eeprom.getPM1CalibrationOffset())         + "}";
}


bool needToConnect = false;

// Called the very first time a Birdhouse is booted -- set defaults
void intitialConfig() {
  if(String(Eeprom.getMqttUrl()) == "www.sensorbot.org")
    return;

  paramManager.setParam("localPassword",   "88888888");  
  paramManager.setParam("localSsid",       "NewBirdhouse666");  
  paramManager.setParam("wifiPassword",    "NOT_SET");  
  paramManager.setParam("wifiSsid",        "NOT_SET");  
  paramManager.setParam("mqttUrl",         "www.sensorbot.org");  
  paramManager.setParam("mqttPort",        "1883");  
  paramManager.setParam("deviceToken",     "NOT_SET");  
  paramManager.setParam("sampleDuration",  "30");  
  paramManager.setParam("serialNumber",    "-1");  
  
  paramManager.setParam("temperatureCalibrationFactor", "1.0");
  paramManager.setParam("temperatureCalibrationOffset", "0");
  paramManager.setParam("humidityCalibrationFactor",    "1.0");
  paramManager.setParam("humidityCalibrationOffset",    "0");
  paramManager.setParam("pressureCalibrationFactor",    "1.0");
  paramManager.setParam("pressureCalibrationOffset",    "0");
  paramManager.setParam("pM10CalibrationFactor",        "1.0");
  paramManager.setParam("pM10CalibrationOffset",        "0");
  paramManager.setParam("pM25CalibrationFactor",        "1.0");
  paramManager.setParam("pM25CalibrationOffset",        "0");
  paramManager.setParam("pM1CalibrationFactor",         "1.0");
  paramManager.setParam("pM1CalibrationOffset",         "0");
}


bool isConnectingToWifi = false;    // True while a connection is in process

U32 wifiConnectStartTime = 0;

U32 serialSwappedTime;

U32 firstMillis = 0;

void loop() {
  bool first = firstMillis == 0;

  if(first)
    firstMillis = millis();

  now_millis = millis();

  ledUtils.loop();

  if(!serialSwapped)
    Rest.handle(Serial);


  loopPubSub();


  // TODO: Make this non-blocking
  if(first || now_millis - lastScanTime > 30 * MINUTES) {
    scanVisibleNetworks();    // Blocking
    lastScanTime = millis();
  }


  // First time here, let's look for a Plantower sensor
  if(first && !serialSwapped && !plantowerSensorDetected) {
    Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
    Serial.swap();
    serialSwapped = true;
  }

  if(serialSwapped && !plantowerSensorDetected && pms.read(PmsData)) 
    plantowerSensorDetected = true;

  // Look for 20 seconds or until we detect a sensor
  if((millis() - firstMillis > 20 * SECONDS || plantowerSensorDetected) && serialSwapped) {
    Serial.begin(SERIAL_BAUD_RATE);   // Serial.begin() resets our earlier swap
    serialSwapped = false;

    Serial.printf("Plantower sensor %s\n", plantowerSensorDetected ? "detected" : "not found");
  }


  if(isConnectingToWifi) {
    connectingToWifi();
  }

  if(!isConnectingToWifi && millis() - wifiConnectStartTime > 1 * SECONDS)
    connectToWiFi(changedWifiCredentials);


  first = false;

}


int ledsHandler(String params) {

  vector<pair<String,String>> kvPairs;
  ParameterManager::parse(params, kvPairs);

  for(int i = 0; i < kvPairs.size(); i++) {
    // setParam(kvPairs[i].first, kvPairs[i].second);

    if(kvPairs[i].first.equalsIgnoreCase("color")) {
      if(kvPairs[i].second.equalsIgnoreCase("red"))
        ledUtils.setBlinkPattern(LedUtils::SOLID_RED);

      if(kvPairs[i].second.equalsIgnoreCase("yellow"))
        ledUtils.setBlinkPattern(LedUtils::SOLID_YELLOW);

      if(kvPairs[i].second.equalsIgnoreCase("green"))
        ledUtils.setBlinkPattern(LedUtils::SOLID_GREEN);

      if(kvPairs[i].second.equalsIgnoreCase("all"))
        ledUtils.setBlinkPattern(LedUtils::ALL_ON);

      if(kvPairs[i].second.equalsIgnoreCase("off"))
        ledUtils.setBlinkPattern(LedUtils::OFF);
    }
  }

  return 1;
}


int updateFirmware(String param) {
  if(WiFi.status() != WL_CONNECTED)
    return 0;

  // Where do we check for firmware updates?
  #define FIRMWARE_UPDATE_SERVER "www.sensorbot.org"
  #define FIRMWARE_UPDATE_PORT 8989


  t_httpUpdate_return ret = ESPhttpUpdate.update(FIRMWARE_UPDATE_SERVER, FIRMWARE_UPDATE_PORT, "/update/", "0.0");

  switch(ret) {
    case HTTP_UPDATE_FAILED:
        break;
    case HTTP_UPDATE_NO_UPDATES:
        break;
    case HTTP_UPDATE_OK:    // Never get here because if there is an update, the update() function will take over and we'll reboot
        break;
  }

  return 1;
}


int rebootHandler(String params) {
  ESP.restart();
  return 1;
}

bool BME_ok = false;

// Temperature sensor
BME280I2C bme;


// Orange: VCC, yellow: GND, Green: SCL, d1, Blue: SDA, d2
void setupSensors() {
  // Temperature, humidity, and barometric pressure
  Wire.begin(BME_SDA, BME_SCL);
  BME_ok = bme.begin();

  if(BME_ok) {

    BME280I2C::Settings settings(
           BME280::OSR_X16,             // Temp   --> default was OSR_1X
           BME280::OSR_X16,             // Humid  --> default was OSR_1X
           BME280::OSR_X16,             // Press
           BME280::Mode_Forced,         // Power mode --> Forced is recommended by datasheet
           BME280::StandbyTime_1000ms, 
           BME280::Filter_Off,          // Pressure filter
           BME280::SpiEnable_False,
           0x76                         // I2C address. I2C specific.
        );

    // Temperature sensor
    bme.setSettings(settings);

    F32 t = bme.temp();
  }
}


const char *getTemperatureSensorName() {
  if(!BME_ok)
    return "No temperature sensor detected";

  switch(bme.chipModel()) {
    case BME280::ChipModel_BME280:
      return "BME280 (temperature and humidity)";
    case BME280::ChipModel_BMP280:
      return "BMP280 (temperature, no humidity)";
    default:
      return "No temperature or humidity sensor found";
  }
}


// Blocking scan
void scanVisibleNetworks() {
  WiFi.scanNetworks(false, true);    // Include hidden access points

  U32 scanStartTime = millis();

  // Wait for scan to complete with max 30 seconds
  while(WiFi.scanComplete() == WIFI_SCAN_RUNNING) { 
    delay(1);
    if(millis() - scanStartTime > 30 * SECONDS) {
      return;
    }
  }
}


void onMqttPortUpdated() {
  if(Eeprom.getMqttPort() == 0)
    return;

  mqtt.mqttDisconnect();
  mqttServerConfigured = false;
  mqttServerLookupError = false;

  setupPubSubClient();

  // Let's immediately connect our PubSub client
  reconnectToPubSubServer();
}


void setWifiSsidFromScanResults(int index) {
  if(WiFi.scanComplete() < 0) {
    return;
  }

  // Networks are 0-indexed, but user will be selecting a network based on 1-indexed display
  if(index < 1 || index > WiFi.scanComplete()) {
    return;
  }
  
  paramManager.setParam("wifiSsid", WiFi.SSID(index - 1));  
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


// Get a list of wifi hotspots the device can see, and put them into a chunk of JSON
String getScanResults() {
  int networksFound = WiFi.scanComplete();

  if(networksFound <= 0) {
    "\"Scan not complete\"";
  }

  String json = "["; 

  for (int i = 0; i < networksFound; i++) {
    json += "{\"ssid\":\"" + (WiFi.SSID(i) == "" ? "[Hidden network]" : WiFi.SSID(i)) + "\",\"macAddress\":\"" + WiFi.BSSIDstr(i) + "\",\"signalStrength\":" + String(WiFi.RSSI(i)) + 
                ", \"channel\":" + String(WiFi.channel(i)) + " }";
    if(i < networksFound - 1)
      json += ",";
  }
  json += "]";

  return json;
}


void ping(const char *target) {
  connectToWiFi(false);

  U8 pingCount = 5;
  while(pingCount > 0)
  {
    if(Ping.ping(target, 1)) {
      pingCount--;
      if(pingCount == 0) {
      }
    } else {
      pingCount = 0;    // Cancel ping if it's not working
    }
  }
}


// Called from setup
void setupLocalAccessPoint(const char *ssid, const char *password)
{
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


void connectToWiFi(bool disconnectIfConnected) {

  mqttServerLookupError = false;
  changedWifiCredentials = false;

  if(WiFi.status() == WL_CONNECTED) {   // Already connected
    if(!disconnectIfConnected)          // Don't disconnect, so nothing to do
      return;

    if(!serialSwapped)
      Serial.println("Disconnecting WiFi");

    WiFi.disconnect();
  }

  auto status = WiFi.begin(getWifiSsid(), getWifiPassword());
  wifiConnectStartTime = millis();    

  if(!serialSwapped)
    Serial.printf("Connecting to %s/%s...\n", getWifiSsid(), getWifiPassword());

  if(status != WL_CONNECT_FAILED) { 
    isConnectingToWifi = true;
  }
}



// Will be run every loop cycle if isConnectingToWifi is true
void connectingToWifi()
{
  if(WiFi.status() == WL_CONNECTED) {   // We just connected!  :-)

    onConnectedToWifi();
    isConnectingToWifi = false;
    return;
  }

  // Still not connected  :-(

  if(millis() - wifiConnectStartTime > 20 * SECONDS) {
    isConnectingToWifi = false;
  }
}


// We just connected (or reconnected) to wifi
void onConnectedToWifi() {
  if(!serialSwapped)
    Serial.println("Connected to wifi");
}
