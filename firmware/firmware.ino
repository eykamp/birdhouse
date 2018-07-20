// TODO: No AQ readings for 1 min after turned on (as per specs)
// TODO: No AQ measurements when humidity > 95% (as per specs)

#define AREST_NUMBER_VARIABLES 40
#define AREST_NUMBER_FUNCTIONS 20
#define AREST_PARAMS_MODE 1
#define AREST_BUFFER_SIZE 4000

#include <ESP8266WebServer.h>   // Include the WebServer library
#include <BME280I2C.h>
#include <ArduinoJson.h>
#include <Wire.h>

#include "Types.h"
#include "Intervals.h"
#include "BirdhouseEeprom.h"
#include "MqttUtils.h"
#include "ParameterManager.h"
#include "LedUtils.h"
#include "WifiUtils.h"


#include <PMS.h>                  // Plantower
#include <ESP8266httpUpdate.h>    // OTA Updates
#include "c:/dev/aREST/aREST.h"   // Our REST server for provisioning

#include "ESP8266Ping.h"          // For ping, of course
#include "Filter.h"




#define FIRMWARE_VERSION "0.136" // Changing this variable name will require changing the build file to extract it properly

#define TEMPERATURE_UNIT BME280::TempUnit_Celsius
#define PRESSURE_UNIT    BME280::PresUnit_hPa


#define BME_SCL D5  // SPI (Serial Clock)
#define BME_SDA D6  // SDA (Serial Data) 

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
WiFiServer server(REST_LISTEN_PORT);    // Set up a webserver listening on port 80

#define SERIAL_BAUD_RATE 115200
#define PLANTOWER_SERIAL_BAUD_RATE 9600

// Plantower config
PMS pms(Serial);
PMS::DATA PmsData;


// Where do we check for firmware updates?
#define FIRMWARE_UPDATE_SERVER "www.sensorbot.org"
#define FIRMWARE_UPDATE_PORT 8989



// Create a new exponential filter with a weight of 5 and an initial value of 0. 
// Bigger numbers hew truer to the unfiltered data
ExponentialFilter<F32> TemperatureSmoothingFilter(30, 0);



bool plantowerSensorDetected = false;
bool plantowerSensorDetectReported = false;
bool plantowerSensorNondetectReported = false;

U32 lastMqttConnectAttempt = 0;
U32 timeOfLastContact = 0;

ParameterManager paramManager;


const char *localAccessPointAddress = "192.168.1.1";    // Url a user connected by wifi would use to access the device server

void handlesetWifiConnection(); // Forward declare



// A package just arrived!
// { "LED" : "ALL_ON" }
// { "calibrationFactors" : {"temperatureCalibrationFactor" : 1.0, ...}}
void messageReceivedFromMothership(char *topic, byte *payload, unsigned int length) {
  // See https://github.com/bblanchon/ArduinoJson for usage
  StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(payload);

  // Our LED package
  if(mqtt.containsKey(root, "LED"))
    ledUtils.setBlinkPatternByName(root["LED"]);

  // Our calibrationFactors package
  if(mqtt.containsKey(root, "calibrationFactors")) {
    // Create a block of code for every item in NUMERIC_FIELD_LIST that looks like this:
    // if(1 == 1 && mqtt.containsKey(root["calibrationFactors"], "temperatureCalibrationFactor"]))
    //   setTemperatureCalibrationFactor(root["calibrationFactors"]["temperatureCalibrationFactor"]);
    // ...
    // We use the group field to only include the values that will be part of the calibration dataset
    #define FIELD(name, b, c, group, e, g, setterFn)                                  \
        if(group == 1 && mqtt.containsKey(root["calibrationFactors"], #name))    \
          Eeprom.setterFn(root["calibrationFactors"][#name]);
        NUMERIC_FIELD_LIST  
    #undef FIELD

    mqtt.publishCalibrationFactors(getCalibrationJson());
  }
}


U32 lastMillis = 0;
U32 lastScanTime = 0;
U32 lastUpdateCheckTime = 0;

U32 initialFreeHeap;

bool mqttServerConfigured = false;
bool mqttServerLookupError = false;

bool reportedResetReason = false;
U32 mqttServerLookupErrorTimer = 0;

bool serialSwapped = false;     // Track who is using the serial port

void setupPubSubClient() {
  if(millis() - mqttServerLookupErrorTimer > 5 * SECONDS)
    mqttServerLookupError = false;

  if(mqttServerConfigured || mqttServerLookupError)    // If we're already configured, or we've failed at attempting to do so, don't try again
    return;

  if(!wifiUtils.isConnected())          // No point in doing anything here if we don't have internet access
    return;

  IPAddress serverIp; 
  IPAddress fallbackServerIp(162,212,157,80);   // 162.212.157.80

  bool dnsLookupOk = (WiFi.hostByName(Eeprom.getMqttUrl(), serverIp) == 1);

  if(!dnsLookupOk) {
    if(!serialSwapped)
      Serial.println("DNS failed: using fallback IP address.");
  }

  mqtt.mqttSetServer(dnsLookupOk ? serverIp : fallbackServerIp, Eeprom.getMqttPort());
  mqttServerConfigured = true;
}


U32 lastPubSubConnectAttempt = 0;
bool wasConnectedToPubSubServer = false;

void loopPubSub() {
  setupPubSubClient();

  if(!mqttServerConfigured)
    return;

  // Ensure constant contact with the mother ship
  if(!mqtt.mqttConnected()) {
    if(wasConnectedToPubSubServer) {
      onDisconnectedFromPubSubServer();
      wasConnectedToPubSubServer = false;
    }
  
    if(millis() - lastPubSubConnectAttempt > 5 * SECONDS) {
      reconnectToPubSubServer();      // Attempt to reconnect
      lastPubSubConnectAttempt = millis();
      return;
    }
  }

  mqtt.mqttLoop();
}


void onDisconnectedFromPubSubServer() {
  if(!serialSwapped)
    Serial.println("Disconnected from mqtt server... will try to reconnect.");
}


// Gets run when we're not connected to the PubSub server
void reconnectToPubSubServer() {
  if(!mqttServerConfigured)
    return;

  if(!wifiUtils.isConnected())   // No point in doing anything here if we don't have internet access
    return;

  if(!serialSwapped)
    Serial.println("Connecting to mqtt server");

  // Attempt to connect
  lastMqttConnectAttempt = millis();
  if(mqtt.mqttConnect("Birdhouse", Eeprom.getDeviceToken(), "")) {   // ClientID, username, password
    onConnectedToPubSubServer();
  }
}


// Gets run when we successfuly connect to PubSub server
void onConnectedToPubSubServer() {
  if(!serialSwapped)
    Serial.println("Connected to mqtt server!");

  mqtt.mqttSubscribe("v1/devices/me/attributes");                           // ... and subscribe to any shared attribute changes

  // Announce ourselves to the server
  mqtt.publishLocalCredentials(Eeprom.getLocalSsid(), Eeprom.getLocalPassword(), localAccessPointAddress);
  mqtt.publishLocalIp(WiFi.localIP());
  
  mqtt.publishSampleDuration(getSampleDuration());
  mqtt.publishTempSensorNameAndSoftwareVersion(getTemperatureSensorName(), FIRMWARE_VERSION, WiFi.macAddress());
  mqtt.publishStatusMessage("Connected");


  reportPlantowerSensorStatus();
  reportResetReason();
  mqtt.publishCalibrationFactors(getCalibrationJson());    // report calibration factors

  resetDataCollection();                                      // Now we can start our data collection efforts
  ledUtils.setBlinkPattern(LedUtils::SOLID_GREEN);
}



U32 plantowerPm1Sum = 0;
U32 plantowerPm25Sum = 0;
U32 plantowerPm10Sum = 0;
U16 plantowerSampleCount = 0;


U32 samplingPeriodStartTime;

bool initialConfigMode = false;


const char *defaultPingTargetHostName = "www.google.com";


const char *getWifiStatus() {
  return wifiUtils.getWifiStatusName();
}

const char *getMqttStatus() {
  return mqtt.getMqttStatus();
}

int setParamsHandler(String params) {   // Can't be const ref because of aREST design
  return paramManager.setParamsHandler(params);
}


bool disablePlantower = false;

int serialHandler(String params) {
  if(!serialSwapped)
    disablePlantower = true;
}


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
  Serial.printf("Firmware Version %s\n", FIRMWARE_VERSION);
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

  Rest.variable("uptime",             &millis);
  Rest.variable("mqttStatus",         &getMqttStatus);
  Rest.variable("lastMqttConnectAttempt", &lastMqttConnectAttempt);
  Rest.variable("wasConnectedToPubSubServer", &wasConnectedToPubSubServer);
  Rest.variable("mqttConnected",       &getMqttConnected);
  Rest.variable("lastUpdateCheckTime", &lastUpdateCheckTime);


  Rest.variable("wifiStatus",         &getWifiStatus);


  Rest.variable("mqttServerConfigured",             &mqttServerConfigured);
  Rest.variable("mqttServerLookupError",            &mqttServerLookupError);
  Rest.variable("ledStyle",           &getLedStyle);
  Rest.variable("sensorsDetected",    &getSensorsDetected,    false);      // false ==> We'll handle quoting ourselves
  Rest.variable("plantowerDisabled",  &disablePlantower);      
  Rest.variable("calibrationFactors", &getCalibrationJson, false);

  Rest.variable("sampleCount",        &plantowerSampleCount);
  Rest.variable("sampleDurations",    &getSampleDuration);
  Rest.variable("deviceToken",        &getDeviceToken);
  Rest.variable("localSsid",          &getLocalSsid);
  Rest.variable("localPass",          &getLocalPassword);
  Rest.variable("wifiSsid",           &getWifiSsid);
  Rest.variable("wifiPass",           &getWifiPassword);
  Rest.variable("mqttUrl",            &getMqttUrl);
  Rest.variable("mqttPort",           &getMqttPort);
  Rest.variable("serialNumber",       &getBirdhouseNumber);
  Rest.variable("firmwareVersion",    &getFirmwareVersion);

  // These all take a single parameter specified on the cmd line
  Rest.function("setparams", setParamsHandler);
  Rest.function("serial",    serialHandler);
  Rest.function("restart",   rebootHandler);



  Rest.set_id("brdhse");  // Should be 9 chars or less
  Rest.set_name(Eeprom.getLocalSsid());

  wifiUtils.begin();
  wifiUtils.setOnConnectedToWifiCallback        (onConnectedToWifiCallback);
  wifiUtils.setOnConnectedToWifiTimedOutCallback(onConnectedToWifiTimedOutCallback);
  wifiUtils.setOnConnectedToWifiFailedCallback  (onConnectedToWifiFailedCallback);
  wifiUtils.setOnDisconnectedFromWifiCallback   (onDisconnectedFromWifiCallback);


  setupPubSubClient();
  mqtt.mqttSetCallback(messageReceivedFromMothership);

  paramManager.setLocalCredentialsChangedCallback([]()    { mqtt.publishLocalCredentials(Eeprom.getLocalSsid(), Eeprom.getLocalPassword(), localAccessPointAddress); });
  paramManager.setWifiCredentialsChangedCallback([]()     { wifiUtils.setChangedWifiCredentials(); });
  paramManager.setMqttCredentialsChangedCallback(onMqttPortUpdated);
  paramManager.setLedStyleChangedCallback(updateLedStyle);

  setupSensors();
 
  wifiUtils.setupLocalAccessPoint(Eeprom.getLocalSsid(), Eeprom.getLocalPassword(), localAccessPointAddress);

  // Flash yellow until we've connected via wifi
  ledUtils.setBlinkPattern(LedUtils::FAST_BLINK_YELLOW);


  server.begin();   // Start the web server

  initialFreeHeap = ESP.getFreeHeap();
}


bool getMqttConnected() {
  return mqtt.mqttConnected();
}


void updateLedStyle() {
  ledUtils.setLedStyle(static_cast<ParameterManager::LedStyle>(Eeprom.getLedStyle()));
}


// Gets run once, at startup
void handleUpgrade() {
  // Did we just upgrade our firmware?  If so, is there anything we need to do?

  if(String(Eeprom.getFirmwareVersion()) == FIRMWARE_VERSION) {
    return;
  }

  Eeprom.setFirmwareVersion(FIRMWARE_VERSION);

  // ESP.restart();
}


String getLedStyle() {
  return ParameterManager::getLedStyleName((ParameterManager::LedStyle) Eeprom.getLedStyle());
}


String getSensorsDetected() {
  return String("{\"plantowerSensorDetected\":") + (plantowerSensorDetected ? "true" : "false") + ",\"temperatureSensor\":\"" + getTemperatureSensorName() + "\"}";
}


// Returns a JSON object listing all our calibration factors
String getCalibrationJson() {
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



const char *getDeviceToken()     { return Eeprom.getDeviceToken();     }
const char *getLocalSsid()       { return Eeprom.getLocalSsid();       }
const char *getLocalPassword()   { return Eeprom.getLocalPassword();   }
const char *getWifiSsid()        { return Eeprom.getWifiSsid();        }
const char *getWifiPassword()    { return Eeprom.getWifiPassword();    }
const char *getMqttUrl()         { return Eeprom.getMqttUrl();         }
const char *getFirmwareVersion() { return Eeprom.getFirmwareVersion(); }
U16 getSampleDuration()          { return Eeprom.getSampleDuration();  }
U16 getMqttPort()                { return Eeprom.getMqttPort();        }
U16 getBirdhouseNumber()         { return Eeprom.getBirdhouseNumber(); }


U32 millisOveflows = 0;

void loop() {

  if(millis() < lastMillis)
    millisOveflows++;

  lastMillis = millis();

  // If we just can't make contact for a while... reboot!
  if(millis() - timeOfLastContact > 10 * MINUTES)
    restart();

  ledUtils.loop();    // Make the LEDs do their magic

  WiFiClient client = server.available();

  if(client) {
    if(!serialSwapped)
      Serial.println("Handling incoming connection data");
    Rest.handle(client);
  }

  // Keep listening on the serial port until we switch it over to the PMS
  if(!serialSwapped) {
    Rest.handle(Serial);
    activatePlantower();  // This will have a little delay built in so that we can deactivate it if we're connected via serial
  }


  wifiUtils.loop();
  loopPubSub();

  if(mqtt.mqttState() == MQTT_CONNECTED)
    loopSensors();


  if(wifiUtils.isConnected() && millis() - lastUpdateCheckTime > 25 * SECONDS) {
    checkForFirmwareUpdates();
    lastUpdateCheckTime = millis();
  }

  checkFreeHeap();
}



// If memory is running down, reboot!
void checkFreeHeap() {
  U32 freeHeap = ESP.getFreeHeap();

  // If we've lost 10K, or down under 10K... these limits are set arbitrarily, and, based on observed behavior, will rarely be hit
  if(freeHeap < initialFreeHeap - 10000 || freeHeap < 10000)
    restart();

}


int rebootHandler(String params) {
  restart();
  return 1;
}


void restart() {
  digitalWrite(0, HIGH);   // Speculative fix for https://github.com/esp8266/Arduino/issues/793, https://github.com/esp8266/Arduino/issues/1017
  digitalWrite(2, HIGH);   
  ESP.restart();
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

    TemperatureSmoothingFilter.SetCurrent(t);
  }

  resetDataCollection();
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


// Read sensors each loop tick
void loopSensors() {

  if(millis() - samplingPeriodStartTime > Eeprom.getSampleDuration() * SECONDS) {

    if(!serialSwapped)
      Serial.printf("Reporting... %d/%d/%d\n", samplingPeriodStartTime, millis(), lastScanTime);

    reportMeasurements();

    if(millis() - lastScanTime > 30 * MINUTES) {  
      mqtt.publishStatusMessage("Scanning");
      
      String results = wifiUtils.scanVisibleNetworks();
      if(results != "")
        mqtt.publishWifiScanResults(results);

      lastScanTime = millis();
    }

    resetDataCollection();
  }


  if(serialSwapped && pms.read(PmsData)) {
    plantowerPm1Sum  += PmsData.PM_AE_UG_1_0;
    plantowerPm25Sum += PmsData.PM_AE_UG_2_5;
    plantowerPm10Sum += PmsData.PM_AE_UG_10_0;
    plantowerSampleCount++;
    plantowerSensorDetected = true;
  }
}

void resetDataCollection() {

  // Start the cycle anew
  samplingPeriodStartTime = millis();
    
  // Reset Plantower data
  plantowerPm1Sum = 0;
  plantowerPm25Sum = 0;
  plantowerPm10Sum = 0;
  plantowerSampleCount = 0;
}


void checkForFirmwareUpdates() {

  if(!wifiUtils.isConnected())
    return;

  ESPhttpUpdate.update(FIRMWARE_UPDATE_SERVER, FIRMWARE_UPDATE_PORT, "/update/", FIRMWARE_VERSION);

  // If we're updating, the update command will reset the device, so if we get here, no update ocurred
}



void reportPlantowerSensorStatus() {
  // Report each status only once
  if(!plantowerSensorDetected && !plantowerSensorNondetectReported) {
    mqtt.reportPlantowerDetectNondetect(false);
    plantowerSensorNondetectReported = true;
  }

  if(plantowerSensorDetected && !plantowerSensorDetectReported) {
    mqtt.reportPlantowerDetectNondetect(true);
    plantowerSensorDetectReported = true;
  }
}


// Do this once
void reportResetReason() {
  if(reportedResetReason)
    return;

  mqtt.publishResetReason(ESP.getResetReason());
  reportedResetReason = true;
}


// Take any measurements we only do once per reporting period, and send all our data to the mothership
void reportMeasurements() {
  if(!mqtt.publishDeviceData(millis(), ESP.getFreeHeap()))
    return;

  timeOfLastContact = millis();

  if(plantowerSampleCount > 0) {
    F64 rawPm1  = (F64(plantowerPm1Sum) / (F64)plantowerSampleCount);
    F64 rawPm25 = (F64(plantowerPm25Sum) / (F64)plantowerSampleCount);
    F64 rawPm10 = (F64(plantowerPm10Sum) / (F64)plantowerSampleCount);

    // Apply calibration factors
    F64 pm1  = rawPm1  * Eeprom.getPM1CalibrationFactor()  + Eeprom.getPM1CalibrationOffset();
    F64 pm25 = rawPm25 * Eeprom.getPM25CalibrationFactor() + Eeprom.getPM25CalibrationOffset();
    F64 pm10 = rawPm10 * Eeprom.getPM10CalibrationFactor() + Eeprom.getPM10CalibrationOffset();
  
    mqtt.publishPmData(pm1, pm25, pm10, rawPm1, rawPm25, rawPm10, plantowerSampleCount);
    reportPlantowerSensorStatus();
  }

  if(BME_ok) {
    F32 pres, temp, hum;

    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);
    TemperatureSmoothingFilter.Filter(temp);

    // Apply calibration factors
    temp = temp * Eeprom.getTemperatureCalibrationFactor() + Eeprom.getTemperatureCalibrationOffset();
    pres = pres * Eeprom.getHumidityCalibrationFactor()    + Eeprom.getHumidityCalibrationOffset();
    hum  = hum  * Eeprom.getPressureCalibrationFactor()    + Eeprom.getPressureCalibrationOffset();

    mqtt.publishWeatherData(temp, TemperatureSmoothingFilter.Current(), hum, pres);
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




void ping(const char *target) {
  if(!wifiUtils.isConnected())
    return;

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


U32 serialSwapTimer = 0;
int connectionFailures = 0;

// We just connected (or reconnected) to wifi
void onConnectedToWifiCallback() {
  if(!serialSwapped)
    Serial.printf("Connected to WiFi! (this is connection #%d)\n", wifiUtils.getConnectionCount());

  connectionFailures = 0;

  ledUtils.setBlinkPattern(LedUtils::FAST_BLINK_GREEN);   // Stop flashing yellow now that we've connected to wifi

  if(serialSwapTimer == 0)
    serialSwapTimer = millis();
}


void onConnectedToWifiTimedOutCallback() {
  if(!serialSwapped)
    Serial.printf("Attempt to connect to wifi timed out, using credentials %s/%s at %d\n", Eeprom.getWifiSsid(), Eeprom.getWifiPassword(), millis());

  ledUtils.setBlinkPattern(LedUtils::FAST_BLINK_RED);  
}


// This is a serious fault... if we get it 5 times in a row, we'll restart to see if that fixes it
void onConnectedToWifiFailedCallback() {
  if(!serialSwapped)
    Serial.println("Attempt to connect to wifi failed with status WL_CONNECT_FAILED"); 

  ledUtils.setBlinkPattern(LedUtils::SLOW_BLINK_RED);

  connectionFailures++;

  if(connectionFailures >= 5)
    restart();
}


// We were connected and now we're not
void onDisconnectedFromWifiCallback() {
  serialSwapped = false;
  Serial.swap();    // Should be totally superflous
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println("Disconnected from WiFi");
  ledUtils.setBlinkPattern(LedUtils::FAST_BLINK_YELLOW);  
}


void activatePlantower()
{  
  if(serialSwapped)           // Already activated!
    return;   

  if(!wifiUtils.isConnected()) 
    return;

  // if(serialSwapTimer == 0)    // This is only 0 if we've never connected to wifi
  //   return;

  // if(millis() - serialSwapTimer < 3 * SECONDS)
  //   return;

  // if(disablePlantower)
  //   return;

  // Switch over to Plantower
  Serial.println("Turning over the serial port to the Plantower... no more messages here.");
  Serial.flush();   // Get any last bits out of there before switching the serial pins

  Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
  Serial.swap();    // D8 is now TX, D7 RX
  // Serial1.begin(115200);
  serialSwapped = true;
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

