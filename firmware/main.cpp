#define AREST_NUMBER_VARIABLES 40
#define AREST_NUMBER_FUNCTIONS 20
#define AREST_PARAMS_MODE 1
#define AREST_BUFFER_SIZE 4000

#define MQTT_JSON_MAX_PACKET_SIZE 1024 // How much memory we allocate for handling JSON

#include <Arduino.h>
#include <ESP8266WebServer.h> // Include the WebServer library
#include "BME280I2C.h"
#include <ArduinoJson.h>
#include <Wire.h>

// #include "bRest.h"

#include "Types.h"
#include "Intervals.h"
#include "BirdhouseEeprom.h"
#include <aREST.h> // Our REST server for provisioning -- MUST BE INCLUDED BEFORE MqttUtils.h
                   // aREst.h does not terminate lines with a /r/n when the token PubSubClient_h
                   // is defined, and that lack of termination causes our serial port i/o
                   // to stall.
                   // We use the PubSubClient library, (it's imported in MqttUtils.h) so we need
                   // to include that after we've brought in the aREST lib.

#include "MqttUtils.h"
#include "ParameterManager.h"
#include "LedUtils.h"
#include "WifiUtils.h"

#include <PMS.h>               // Plantower
#include <ESP8266httpUpdate.h> // OTA Updates

#include "ESP8266Ping.h" // For ping, of course
#include "Filter.h"      // For data smoothing

#define FIRMWARE_VERSION "0.147" // Changing this variable name will require changing the build file to extract it properly

#define TEMPERATURE_UNIT BME280::TempUnit_Celsius
#define PRESSURE_UNIT BME280::PresUnit_hPa

#define BME_SCL D5 // SPI (Serial Clock)
#define BME_SDA D6 // SDA (Serial Data)

#define UNUSED_PIN D3

// Plantower Sensor pins are hardcoded below; they have to be on the serial pins
// Plantower uses D7, D8

// Output LEDs
#define LED_BUILTIN D4
#define LED_GREEN D0
#define LED_YELLOW D1
#define LED_RED D2

///// OR /////
#define LED_DATA_PIN D1
#define LED_CLOCK_PIN D0

//////////////////////
// WiFi Definitions //
//////////////////////

#define REST_LISTEN_PORT 80

aREST Rest = aREST();
WiFiServer server(REST_LISTEN_PORT); // Set up a webserver listening on port 80

#define SERIAL_BAUD_RATE 115200
#define PLANTOWER_SERIAL_BAUD_RATE 9600

// Plantower config
PMS pms(Serial);
PMS::DATA PmsData;

// Where do we check for firmware updates?
#define FIRMWARE_UPDATE_SERVER "192.210.218.130"
#define FIRMWARE_UPDATE_PORT 8989

// Create a new exponential filter with a weight of 5 and an initial value of 0.
// Bigger numbers hew truer to the unfiltered data
ExponentialFilter<F32> TemperatureSmoothingFilter(30, 0);

bool plantowerSensorDetected = false;
bool plantowerSensorDetectReported = false;
bool plantowerSensorNondetectReported = false;

U32 lastMqttConnectAttempt = 0;
U32 timeOfLastContact = 0;

bool serialSwapped = false; // Track who is using the serial port

ParameterManager paramManager;

const char *localAccessPointAddress = "192.168.1.1"; // Url a user connected by wifi would use to access the device server

void handlesetWifiConnection(); // Forward declare

// Allows us to print to the serial port, even if it is being used to collect AQ data
// #define SPRINTF(args...)         \
//   {if (serialSwapped)            \
//     Serial.swap();               \
//   Serial.printf(args);           \
//   Serial.flush();                \
//   if (serialSwapped)             \
//   Serial.swap();}
#define SPRINTF(args...)           \
  {                                \
    Serial.printf(args);           \
    Serial.flush();                \
  }                                \

// Returns a JSON object listing all our calibration factors
String getCalibrationJson()
{
  return String("{") +
         "\"temperatureCalibrationFactor\":" + String(Eeprom.getTemperatureCalibrationFactor()) + "," +
         "\"temperatureCalibrationOffset\":" + String(Eeprom.getTemperatureCalibrationOffset()) + "," +
         "\"humidityCalibrationFactor\":" + String(Eeprom.getHumidityCalibrationFactor()) + "," +
         "\"humidityCalibrationOffset\":" + String(Eeprom.getHumidityCalibrationOffset()) + "," +
         "\"pressureCalibrationFactor\":" + String(Eeprom.getPressureCalibrationFactor()) + "," +
         "\"pressureCalibrationOffset\":" + String(Eeprom.getPressureCalibrationOffset()) + "," +
         "\"pm10CalibrationFactor\":" + String(Eeprom.getPM10CalibrationFactor()) + "," +
         "\"pm10CalibrationOffset\":" + String(Eeprom.getPM10CalibrationOffset()) + "," +
         "\"pm25CalibrationFactor\":" + String(Eeprom.getPM25CalibrationFactor()) + "," +
         "\"pm25CalibrationOffset\":" + String(Eeprom.getPM25CalibrationOffset()) + "," +
         "\"pm1CalibrationFactor\":" + String(Eeprom.getPM1CalibrationFactor()) + "," +
         "\"pm1CalibrationOffset\":" + String(Eeprom.getPM1CalibrationOffset()) + "}";
}


// A package just arrived!
// { "LED" : "ALL_ON" }
// { "calibrationFactors" : {"temperatureCalibrationFactor" : 1.0, ...}}
void messageReceivedFromMothership(char *topic, byte *payload, unsigned int length)
{

  if (!serialSwapped)
  {
    Serial.println("Incoming from mothership:");
    Serial.println((char *)payload);
  }

  // See https://github.com/bblanchon/ArduinoJson for usage
  StaticJsonDocument<MQTT_JSON_MAX_PACKET_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error)
  {
    SPRINTF("Could not parse JSON from mothership: %s > %s\n", error.c_str(), (char *)payload);
    return;
  }

  // Our LED package
  if (doc.containsKey("LED"))
    ledUtils.setBlinkPatternByName(doc["LED"]);

  // Our calibrationFactors package
  if (doc.containsKey("calibrationFactors"))
  {
    const char *innerRootJson = doc["calibrationFactors"];

    // Inner JSON is a string, which needs to be destringified, thanks to the lack of nested attribtues on Thingsboard
    DeserializationError error = deserializeJson(doc, innerRootJson);
    if (error)
    {
      SPRINTF("Could not parse calibration JSON: %s > %s\n", error.c_str(), (char *)payload);
      return;
    }

// Create a block of code for every item in NUMERIC_FIELD_LIST that looks like this:
// if(1 == 1 && mqtt.containsKey(root["calibrationFactors"], "temperatureCalibrationFactor")) {
//   SPRINTF("Setting %s to %s", "temperatureCalibrationFactor", doc["temperatureCalibrationFactor"]);
//   setTemperatureCalibrationFactor(root["calibrationFactors"]["temperatureCalibrationFactor"]);
// }
// ...
// We use the group field to only include the values that will be part of the calibration dataset
#define FIELD(name, b, c, group, e, f, setterFn)                   \
  if (group == 1 && doc.containsKey(#name))                        \
  {                                                                \
    if (!serialSwapped)                                            \
      SPRINTF("Setting %s to %s", #name, (const char *)doc[#name]); \
    Eeprom.setterFn(doc[#name]);                                   \
  }
    NUMERIC_FIELD_LIST
#undef FIELD

    // Tell server the new calibration factors we're using
    mqtt.publishCalibrationFactors(getCalibrationJson());
  }
}

U32 lastMillis = 0;
U32 lastScanTime = 0;
U32 lastUpdateCheckTime = 0;

bool mqttServerConfigured = false;
bool mqttServerLookupError = false;
int dnsLookupSuccessCode;

bool reportedResetReason = false;
U32 mqttServerLookupErrorTimer = 0;

void setupPubSubClient()
{
  // Reset error status after 5 seconds to prevent us from continually hammering the server
  if (millis() - mqttServerLookupErrorTimer > 5 * SECONDS)
    mqttServerLookupError = false;

  if (mqttServerConfigured || mqttServerLookupError) // If we're already configured, or we've failed at attempting to do so, don't try again
    return;

  if (!wifiUtils.isConnected()) // No point in doing anything here if we don't have internet access
    return;

  IPAddress serverIp;
  IPAddress fallbackServerIp(192, 210, 218, 130); // 192.210.218.130 ==> sensorbot.org 3.0

  dnsLookupSuccessCode = WiFi.hostByName(Eeprom.getMqttUrl(), serverIp);

  if (dnsLookupSuccessCode == 1) {
    SPRINTF("DNS lookup successful!\n");
  }
  else {
    serverIp = fallbackServerIp;
    SPRINTF("DNS lookup failed: using fallback IP address. Error: %d\n", dnsLookupSuccessCode);
  }

  mqtt.mqttSetServer(serverIp, Eeprom.getMqttPort());
  mqttServerConfigured = true;
}

void onDisconnectedFromPubSubServer()
{
  if (!serialSwapped)
    Serial.println("Disconnected from mqtt server... will try to reconnect.");
}

void restart()
{
  digitalWrite(0, HIGH); // Speculative fix for https://github.com/esp8266/Arduino/issues/793, https://github.com/esp8266/Arduino/issues/1017
  digitalWrite(2, HIGH);
  ESP.restart();
}


void publishCredentials()
{
  mqtt.publishCredentials(
      Eeprom.getLocalSsid(),
      Eeprom.getLocalPassword(),
      localAccessPointAddress,
      Eeprom.getWifiSsid(),
      Eeprom.getWifiPassword(),
      WiFi.localIP());
}

const char *getDeviceToken() { return Eeprom.getDeviceToken(); }
const char *getLocalSsid() { return Eeprom.getLocalSsid(); }
const char *getLocalPassword() { return Eeprom.getLocalPassword(); }
const char *getWifiSsid() { return Eeprom.getWifiSsid(); }
const char *getWifiPassword() { return Eeprom.getWifiPassword(); }
const char *getMqttUrl() { return Eeprom.getMqttUrl(); }
const char *getFirmwareVersion() { return Eeprom.getFirmwareVersion(); }
U16 getSampleDuration() { return Eeprom.getSampleDuration(); }
U16 getMqttPort() { return Eeprom.getMqttPort(); }
U16 getBirdhouseNumber() { return Eeprom.getBirdhouseNumber(); }


bool BME_ok = false; // Will be set to true once we've established contact with the sensor

// Temperature sensor
BME280I2C bme;


const char *getTemperatureSensorName()
{
  if (!BME_ok)
    return "No temperature sensor detected";

  switch (bme.chipModel())
  {
  case BME280::ChipModel_BME280:
    return "BME280 (temperature and humidity)";
  case BME280::ChipModel_BMP280:
    return "BMP280 (temperature, no humidity)";
  default:
    return "No temperature or humidity sensor found";
  }
}

void reportPlantowerSensorStatus()
{
  // Report each status only once
  if (!plantowerSensorDetected && !plantowerSensorNondetectReported)
  {
    mqtt.reportPlantowerDetectNondetect(false);
    plantowerSensorNondetectReported = true;
  }

  if (plantowerSensorDetected && !plantowerSensorDetectReported)
  {
    mqtt.reportPlantowerDetectNondetect(true);
    plantowerSensorDetectReported = true;
  }
}


// Do this once
void reportResetReason()
{
  if (reportedResetReason)
    return;

  mqtt.publishResetReason(ESP.getResetReason());
  reportedResetReason = true;
}


// Variables for helping us average mutliple readings over our sampling period
U32 plantowerPm1Sum = 0;
U32 plantowerPm25Sum = 0;
U32 plantowerPm10Sum = 0;
U16 plantowerSampleCount = 0;
U32 mqttConnectionAttempts = 0;

U32 samplingPeriodStartTime;

bool initialConfigMode = false;

const char *defaultPingTargetHostName = "www.google.com";



void resetDataCollection()
{
  // Start the cycle anew
  samplingPeriodStartTime = millis();

  // Reset Plantower data
  plantowerPm1Sum = 0;
  plantowerPm25Sum = 0;
  plantowerPm10Sum = 0;
  plantowerSampleCount = 0;
}




// Gets run when we successfuly connect to PubSub server
void onConnectedToPubSubServer()
{
  if (!serialSwapped)
    Serial.println("Connected to mqtt server!");

  mqtt.mqttSubscribe("v1/devices/me/attributes"); // ... and subscribe to any shared attribute changes

  // Announce ourselves to the server
  publishCredentials();


  mqtt.publishSampleDuration(getSampleDuration());
  mqtt.publishTempSensorNameAndSoftwareVersion(getTemperatureSensorName(), FIRMWARE_VERSION, WiFi.macAddress());
  mqtt.publishStatusMessage("Connected");

  if (dnsLookupSuccessCode == 1)
    mqtt.publishDnsResult("Ok");
  else
    mqtt.publishDnsResult("Err " + String(dnsLookupSuccessCode));

  reportPlantowerSensorStatus();
  reportResetReason();
  mqtt.publishCalibrationFactors(getCalibrationJson()); // report calibration factors

  resetDataCollection(); // Now we can start our data collection efforts
  ledUtils.setBlinkPattern(LedUtils::CONNECTED_TO_MQTT_SERVER);

  mqttConnectionAttempts = 0;
}

// Gets run when we're not connected to the PubSub server
void reconnectToPubSubServer()
{
  if (!mqttServerConfigured)
    return;

  if (!wifiUtils.isConnected()) // No point in doing anything here if we don't have internet access
    return;

  mqttConnectionAttempts++;

  // We've observed crashes after several dozen failed connection attempts.  Not sure why, but this may address the symptom.
  if (mqttConnectionAttempts > 16)
    restart();

  SPRINTF("Connecting to mqtt server (attempt %d)\n", mqttConnectionAttempts);

  // Attempt to connect
  lastMqttConnectAttempt = millis();
  if (mqtt.mqttConnect("Birdhouse", Eeprom.getDeviceToken(), ""))
  { // ClientID, username, password
    onConnectedToPubSubServer();
  }
}

U32 lastPubSubConnectAttempt = 0;
bool wasConnectedToPubSubServer = false;

void loopPubSub()
{
  setupPubSubClient();

  if (!mqttServerConfigured)
    return;

  // Ensure constant contact with the mother ship
  if (!mqtt.mqttConnected())
  {
    if (wasConnectedToPubSubServer)
    {
      onDisconnectedFromPubSubServer();
      wasConnectedToPubSubServer = false;
    }

    if (millis() - lastPubSubConnectAttempt > 5 * SECONDS)
    {
      reconnectToPubSubServer(); // Attempt to reconnect
      lastPubSubConnectAttempt = millis();
      return;
    }
  }

  mqtt.mqttLoop();
}


const char *getTemperatureSensorName();   // Forward declare for the moment to get this to build under platformIO


const char *getWifiStatus()
{
  return wifiUtils.getWifiStatusName();
}

const char *getMqttStatus()
{
  return mqtt.getMqttStatus();
}

int setParamsHandler(String params)
{ // Can't be const ref because of aREST design
  return paramManager.setParamsHandler(params);
}

bool disablePlantower = false;

int serialHandler(String params)
{
  if (!serialSwapped)
    disablePlantower = true;

  return 0;
}


U32 initialFreeHeap;    // Track memory to see if there's a leak


// If memory is running down, reboot!
void checkFreeHeap()
{
  U32 freeHeap = ESP.getFreeHeap();

  // If we've lost 10K, or down under 10K... these limits are set arbitrarily, and, based on observed behavior, will rarely be hit
  if (freeHeap < initialFreeHeap - 10000 || freeHeap < 10000)
    restart();
}


// Orange: VCC, yellow: GND, Green: SCL, d1, Blue: SDA, d2
void setupSensors()
{
  // Temperature, humidity, and barometric pressure
  Wire.begin(BME_SDA, BME_SCL);
  BME_ok = bme.begin();

  if (BME_ok)
  {
    BME280I2C::Settings settings(
        BME280::OSR_X16,     // Temp   --> default was OSR_1X
        BME280::OSR_X16,     // Humid  --> default was OSR_1X
        BME280::OSR_X16,     // Press
        BME280::Mode_Forced, // Power mode --> Forced is recommended by datasheet
        BME280::StandbyTime_1000ms,
        BME280::Filter_Off, // Pressure filter
        BME280::SpiEnable_False,
        BME280I2C::I2CAddr_0x76 // I2C address. Device specific.
    );

    // Temperature sensor
    bme.setSettings(settings);

    TemperatureSmoothingFilter.SetCurrent(bme.temp()); // Seed the filter
  }

  resetDataCollection();
}

bool hasHumidity()
{
  return BME_ok && bme.chipModel() == BME280::ChipModel_BME280;
}


void onMqttPortUpdated()
{
  if (Eeprom.getMqttPort() == 0)
    return;

  mqtt.mqttDisconnect();
  mqttServerConfigured = false;
  mqttServerLookupError = false;

  setupPubSubClient();

  // Let's immediately connect our PubSub client
  reconnectToPubSubServer();
}


// We were connected and now we're not
void onDisconnectedFromWifiCallback()
{
  // serialSwapped = false;
  // Serial.swap(); // Should be totally superflous
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println("Disconnected from WiFi");
  ledUtils.setBlinkPattern(LedUtils::DISCONNECTED_FROM_WIFI);
}

U32 serialSwapTimer = 0;
int connectionFailures = 0;

// We just connected (or reconnected) to wifi
void onConnectedToWifiCallback()
{
  SPRINTF("Connected to WiFi! (this is connection #%d)\n", wifiUtils.getConnectionCount());

  connectionFailures = 0;

  ledUtils.setBlinkPattern(LedUtils::CONNECTED_TO_WIFI); // Stop flashing yellow now that we've connected to wifi

  if (serialSwapTimer == 0)
    serialSwapTimer = millis();
}

void onConnectedToWifiTimedOutCallback()
{
  SPRINTF("Attempt to connect to wifi timed out, using credentials %s/%s at %ld\n", Eeprom.getWifiSsid(), Eeprom.getWifiPassword(), millis());

  ledUtils.setBlinkPattern(LedUtils::WIFI_CONNECT_TIMED_OUT);
}

// This is a serious fault... if we get it 5 times in a row, we'll restart to see if that fixes it
void onConnectedToWifiFailedCallback()
{
  if (!serialSwapped)
    Serial.println("Attempt to connect to wifi failed with status WL_CONNECT_FAILED");

  ledUtils.setBlinkPattern(LedUtils::WIFI_CONNECT_FAILED);

  connectionFailures++;

  if (connectionFailures >= 5)
    restart();
}


int rebootHandler(String params)
{
  restart();
  return 1;
}


void updateLedStyle()
{
  ledUtils.setLedStyle(static_cast<ParameterManager::LedStyle>(Eeprom.getLedStyle()));
}


// Gets run once, at startup
void handleUpgrade()
{
  // Did we just upgrade our firmware?  If so, is there anything we need to do?

  if (String(Eeprom.getFirmwareVersion()) == FIRMWARE_VERSION)
  {
    return;
  }

  Eeprom.setFirmwareVersion(FIRMWARE_VERSION);

  // ESP.restart();
}


bool getMqttConnected()
{
  return mqtt.mqttConnected();
}


String getLedStyle()
{
  return ParameterManager::getLedStyleName((ParameterManager::LedStyle)Eeprom.getLedStyle());
}

String getSensorsDetected()
{
  return String("{\"plantowerSensorDetected\":") + (plantowerSensorDetected ? "true" : "false") + ",\"temperatureSensor\":\"" + getTemperatureSensorName() + "\"}";
}




void setup()
{

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
  Serial.printf("Firmware Version %s\n", FIRMWARE_VERSION);
  Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());
  Serial.println("");

  // See https://github.com/esp8266/Arduino/issues/4321
  pinMode(UNUSED_PIN, OUTPUT);
  analogWrite(UNUSED_PIN, 0);
  analogWrite(UNUSED_PIN, 10);

  Eeprom.begin();

  ledUtils.begin(LED_RED, LED_YELLOW, LED_GREEN, LED_BUILTIN, LED_DATA_PIN, LED_CLOCK_PIN);
  updateLedStyle();
  ledUtils.playStartupSequence();

  handleUpgrade();

  Rest.variable("uptime", &millis);
  Rest.variable("mqttStatus", &getMqttStatus);
  Rest.variable("lastMqttConnectAttempt", &lastMqttConnectAttempt);
  Rest.variable("wasConnectedToPubSubServer", &wasConnectedToPubSubServer);
  Rest.variable("mqttConnected", &getMqttConnected);
  Rest.variable("lastUpdateCheckTime", &lastUpdateCheckTime);

  Rest.variable("wifiStatus", &getWifiStatus);

  Rest.variable("mqttServerConfigured", &mqttServerConfigured);
  Rest.variable("mqttServerLookupError", &mqttServerLookupError);
  Rest.variable("ledStyle", &getLedStyle);
  Rest.variable("sensorsDetected", &getSensorsDetected, false); // false ==> We'll handle quoting ourselves
  Rest.variable("plantowerDisabled", &disablePlantower);
  Rest.variable("calibrationFactors", &getCalibrationJson, false);

  Rest.variable("sampleCount", &plantowerSampleCount);
  Rest.variable("sampleDuration", &getSampleDuration);
  Rest.variable("deviceToken", &getDeviceToken);
  Rest.variable("localSsid", &getLocalSsid);
  Rest.variable("localPass", &getLocalPassword);
  Rest.variable("wifiSsid", &getWifiSsid);
  Rest.variable("wifiPass", &getWifiPassword);
  Rest.variable("mqttUrl", &getMqttUrl);
  Rest.variable("mqttPort", &getMqttPort);
  Rest.variable("serialNumber", &getBirdhouseNumber);
  Rest.variable("firmwareVersion", &getFirmwareVersion);

  // These all take a single parameter specified on the cmd line
  Rest.function("setparams", setParamsHandler);
  Rest.function("serial", serialHandler);
  Rest.function("restart", rebootHandler);

  Rest.set_id("brdhse"); // Should be 9 chars or less
  Rest.set_name(Eeprom.getLocalSsid());

  wifiUtils.begin(Eeprom.getLocalSsid());
  wifiUtils.setOnConnectedToWifiCallback(onConnectedToWifiCallback);
  wifiUtils.setOnConnectedToWifiTimedOutCallback(onConnectedToWifiTimedOutCallback);
  wifiUtils.setOnConnectedToWifiFailedCallback(onConnectedToWifiFailedCallback);
  wifiUtils.setOnDisconnectedFromWifiCallback(onDisconnectedFromWifiCallback);

  setupPubSubClient();
  mqtt.mqttSetCallback(messageReceivedFromMothership);

  paramManager.setLocalCredentialsChangedCallback([]() { publishCredentials(); });
  paramManager.setWifiCredentialsChangedCallback([]() { wifiUtils.setChangedWifiCredentials(); });
  paramManager.setMqttCredentialsChangedCallback(onMqttPortUpdated);
  paramManager.setLedStyleChangedCallback(updateLedStyle);

  setupSensors();

  wifiUtils.setupLocalAccessPoint(Eeprom.getLocalSsid(), Eeprom.getLocalPassword(), localAccessPointAddress);

  // Flash yellow until we've connected via wifi
  ledUtils.setBlinkPattern(LedUtils::DISCONNECTED_FROM_WIFI);

  server.begin(); // Start the web server

  initialFreeHeap = ESP.getFreeHeap();
}


void checkForFirmwareUpdates(WiFiClient &client)
{
  if (!wifiUtils.isConnected())
    return;

  ESPhttpUpdate.update(client, FIRMWARE_UPDATE_SERVER, FIRMWARE_UPDATE_PORT, "/update/", FIRMWARE_VERSION);


  // If we're updating, the update command will reset the device, so if we get here, no update ocurred
}

// Take any measurements we only do once per reporting period, and send all our data to the mothership
void reportMeasurements()
{
  if (!mqtt.publishDeviceData(millis(), ESP.getFreeHeap()))
    return;

  timeOfLastContact = millis();

  if (plantowerSampleCount > 0)
  {
    F64 rawPm1 = (F64(plantowerPm1Sum) / (F64)plantowerSampleCount);
    F64 rawPm25 = (F64(plantowerPm25Sum) / (F64)plantowerSampleCount);
    F64 rawPm10 = (F64(plantowerPm10Sum) / (F64)plantowerSampleCount);

    // Apply calibration factors
    F64 pm1 = rawPm1 * Eeprom.getPM1CalibrationFactor() + Eeprom.getPM1CalibrationOffset();
    F64 pm25 = rawPm25 * Eeprom.getPM25CalibrationFactor() + Eeprom.getPM25CalibrationOffset();
    F64 pm10 = rawPm10 * Eeprom.getPM10CalibrationFactor() + Eeprom.getPM10CalibrationOffset();

    mqtt.publishPmData(pm1, pm25, pm10, rawPm1, rawPm25, rawPm10, plantowerSampleCount);
    reportPlantowerSensorStatus();
  }

  if (BME_ok)
  {
    F32 pres, temp, hum;

    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);
    TemperatureSmoothingFilter.Filter(temp);

    // Apply calibration factors
    temp = temp * Eeprom.getTemperatureCalibrationFactor() + Eeprom.getTemperatureCalibrationOffset();
    hum = hum * Eeprom.getHumidityCalibrationFactor() + Eeprom.getHumidityCalibrationOffset();
    pres = pres * Eeprom.getPressureCalibrationFactor() + Eeprom.getPressureCalibrationOffset();

    mqtt.publishWeatherData(temp, TemperatureSmoothingFilter.Current(), hum, pres, hasHumidity());
  }
}


void flushSerial(){
  while(Serial.available()) {
    Serial.read();
  }
}

// Read sensors each loop tick
// This only gets called when we have an MQTT connection
U32 lastLoopSensorsTime = 0;

void loopSensors()
{
  if (millis() - lastLoopSensorsTime < 500)
    return;

  lastLoopSensorsTime = millis();

  if (millis() - samplingPeriodStartTime > Eeprom.getSampleDuration() * SECONDS)
  {
    SPRINTF("Reporting... %d/%ld/%d/%d\n", samplingPeriodStartTime, millis(), lastScanTime, plantowerSampleCount);

    reportMeasurements();

    if (millis() - lastScanTime > 30 * MINUTES)
    {
      mqtt.publishStatusMessage("Scanning");

      String results = wifiUtils.scanVisibleNetworks();
      if (results != "")
        mqtt.publishWifiScanResults(results);

      lastScanTime = millis();
    }
    resetDataCollection();
  }

  if(!serialSwapped) {
    Serial.println("swapping... ");
    Serial.flush();
    Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
    Serial.swap(); // D8 is now TX, D7 RX
    serialSwapped = true;
  }


  // Serial.println(Serial.available() ? "A" : "X");
  if (pms.read(PmsData))
  {
    plantowerPm1Sum += PmsData.PM_AE_UG_1_0;
    plantowerPm25Sum += PmsData.PM_AE_UG_2_5;
    plantowerPm10Sum += PmsData.PM_AE_UG_10_0;
    plantowerSampleCount++;
    plantowerSensorDetected = true;
  }

  // if(!serialSwapped or 1) {
  //   Serial.begin(SERIAL_BAUD_RATE);
  //   delay(5);
    // flushSerial();
    // Serial.swap();
  // }

}

bool plantowerOk = false;

void activatePlantower()
{
  if (!wifiUtils.isConnected())
    return;

  if (plantowerOk) // Already activated!
    return;

  // if(serialSwapTimer == 0)    // This is only 0 if we've never connected to wifi
  //   return;

  if (millis() - serialSwapTimer < 5 * SECONDS)
    return;

  // if(disablePlantower)
  //   return;

  // Switch over to Plantower
  Serial.println("Turning over the serial port to the Plantower... no more messages here.");
  Serial.flush(); // Get any last bits out of there before switching the serial pins

  Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
  Serial.swap(); // D8 is now TX, D7 RX
  plantowerOk = true;
}


U32 millisOveflows = 0;

void loop()
{
  if (millis() < lastMillis)
    millisOveflows++;

  lastMillis = millis();

  // If we just can't make contact for a while... reboot!
  if (millis() - timeOfLastContact > 10 * MINUTES)
    restart();

  ledUtils.loop(); // Make the LEDs do their magic

  WiFiClient client = server.available();

  if (client)
  {
    // Safari often hangs when contacting the device's REST server.  It appears that, on occasion, the client is ready, but
    // the incoming data is not yet present.  This can cause the request to get lost, and an empty response sent.
    // By injecting a small wait, this seems to cure the problem.
    int tries = 0;
    while (!client.available() && tries < 10)
    {
      delay(10);
      tries++;
    }

    SPRINTF("Handling incoming connection data (on attempt %d)\n", tries + 1);
    Rest.handle(client);
  }

  // Keep listening on the serial port until we switch it over to the PMS
  if (!serialSwapped)
  {
    Rest.handle(Serial);
    activatePlantower();  // This will have a little delay built in so that we can deactivate it if we're connected via serial
  }

  wifiUtils.loop();
  loopPubSub();

  if (mqtt.mqttState() == MQTT_CONNECTED)
    loopSensors();

  if (wifiUtils.isConnected() && millis() - lastUpdateCheckTime > 25 * SECONDS)
  {
    checkForFirmwareUpdates(client);
    lastUpdateCheckTime = millis();
  }

  checkFreeHeap();
}

// void setWifiSsidFromScanResults(int index) {
//   if(WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
//     return;
//   }

//   if(WiFi.scanComplete() == WIFI_SCAN_FAILED) {
//     return;
//   }

//   // Networks are 0-indexed, but user will be selecting a network based on 1-indexed display
//   if(index < 1 || index > WiFi.scanComplete()) {
//     return;
//   }

//   paramManager.setParam("wifiSsid", WiFi.SSID(index - 1));
// }

void ping(const char *target)
{
  if (!wifiUtils.isConnected())
    return;

  U8 pingCount = 5;
  while (pingCount > 0)
  {
    if (Ping.ping(target, 1))
    {
      pingCount--;
      if (pingCount == 0)
      {
      }
    }
    else
    {
      pingCount = 0; // Cancel ping if it's not working
    }
  }
}


// enum rst_reason {
//  REASON_DEFAULT_RST = 0, /* normal startup by power on */
//  REASON_WDT_RST = 1, /* hardware watch dog reset */
//  REASON_EXCEPTION_RST = 2, /* exception reset, GPIO status won't change */
//  REASON_SOFT_WDT_RST   = 3,  /* software watch dog reset, GPIO status won't change
//  REASON_SOFT_RESTART = 4, /* software restart ,system_restart , GPIO status won't change */
//  REASON_DEEP_SLEEP_AWAKE = 5, /* wake up from deep-sleep */
//  REASON_EXT_SYS_RST      = 6 /* external system reset */
// };
