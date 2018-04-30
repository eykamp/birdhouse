// TODO: No AQ readings for 1 min after turned on (as per specs)
// TODO: No AQ measurements when humidity > 95% (as per specs)

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

#include "Types.h"
#include "Intervals.h"
#include "BirdhouseEeprom.h"
#include "MqttUtils.h"
#include "ParameterManager.h"


#include <Adafruit_DotStar.h>


#include <PMS.h>                // Plantower

// OTA Updates
#include <ESP8266httpUpdate.h>

#include "c:/dev/aREST/aREST.h"              // Our REST server for provisioning
// #include "aREST2.h"

#include "ESP8266Ping.h"        // For ping, of course
#include "Filter.h"




#define FIRMWARE_VERSION "0.120" // Changing this variable name will require changing the build file to extract it properly

#define TEMPERATURE_UNIT BME280::TempUnit_Celsius
#define PRESSURE_UNIT    BME280::PresUnit_hPa




// Verified
// #define D0    16
// #define D1      5   // I2C Bus SCL (clock)
// #define D2      4   // I2C Bus SDA (data)
// #define D3      0
// #define D4      2   // Also blinks on-board blue LED, but with inverted logic
// #define D5    14   // SPI Bus SCK (clock)
// #define D6    12   // SPI Bus MISO 
// #define D7    13   // SPI Bus MOSI
// #define D8    15   // SPI Bus SS (CS)
// #define D9      3   // RX0 (Serial console)
// #define D10    1   // TX0 (Serial console)

// Pin layout
#define BME_SCL D5  // SPI (Serial Clock)
#define BME_SDA D6  // SDA (Serial Data) 


// Plantower Sensor pins are hardcoded below; they have to be on the serial pins

// Output LEDs
#define LED_BUILTIN D4
#define LED_GREEN D0
#define LED_YELLOW D1
#define LED_RED D2
///// OR /////
#define LED_DATA_PIN D1
#define LED_CLOCK_PIN D0


Adafruit_DotStar strip(1, LED_DATA_PIN, LED_CLOCK_PIN, DOTSTAR_BGR);  // We'll never acutally use this when traditional LEDs are installed, but we don't know that at compile time, and instantiation isn't terribly harmful


//////////////////////
// WiFi Definitions //
//////////////////////


#define REST_LISTEN_PORT 80

aREST Rest = aREST();
WiFiServer server(REST_LISTEN_PORT);    // Set up a webserver listening on port 80

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


enum Leds {
  NONE = 0,
  RED = 1,
  YELLOW = 2,
  GREEN = 4,
  BUILTIN = 8
};

enum BlinkPattern {
  OFF,
  STARTUP,
  ALL_ON,
  SOLID_RED,
  SOLID_YELLOW,
  SOLID_GREEN,
  FAST_BLINK_RED,
  FAST_BLINK_YELLOW,
  FAST_BLINK_GREEN,
  SLOW_BLINK_RED,
  SLOW_BLINK_YELLOW,
  SLOW_BLINK_GREEN,
  ERROR_STATE,
  BLINK_PATTERN_COUNT
};


bool plantowerSensorDetected = false;
bool plantowerSensorDetectReported = false;
bool plantowerSensorNondetectReported = false;


ParameterManager paramManager;

U16 wifiChannel = 11;   // TODO: Delete? EEPROM, 0 = default, 1-13 are valid values

U32 millisOveflows = 0;

const U32 WIFI_CONNECT_TIMEOUT = 20 * SECONDS;

const char *localAccessPointAddress = "192.168.1.1";    // Url a user connected by wifi would use to access the device server
const char *localGatewayAddress = "192.168.1.2";

void handleWifiConnection(); // Forward declare
void activateLed(U32 ledMask);


BlinkPattern blinkPattern = STARTUP;

void messageReceivedFromMothership(char* topic, byte* payload, unsigned int length) {
  return;
  // See https://github.com/bblanchon/ArduinoJson for usage
  StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(payload);

  const char *mode = root["LED"];

  if(     strcmp(mode, "OFF")               == 0)
    setBlinkPattern(OFF);
  else if(strcmp(mode, "STARTUP")           == 0)
    setBlinkPattern(STARTUP);
  else if(strcmp(mode, "ALL_ON")            == 0)
    setBlinkPattern(ALL_ON);
  else if(strcmp(mode, "SOLID_RED")         == 0)
    setBlinkPattern(SOLID_RED);
  else if(strcmp(mode, "SOLID_YELLOW")      == 0)
    setBlinkPattern(SOLID_YELLOW);
  else if(strcmp(mode, "SOLID_GREEN")       == 0)
    setBlinkPattern(SOLID_GREEN);
  else if(strcmp(mode, "FAST_BLINK_RED")    == 0)
    setBlinkPattern(FAST_BLINK_RED);
  else if(strcmp(mode, "FAST_BLINK_YELLOW") == 0)
    setBlinkPattern(FAST_BLINK_YELLOW);
  else if(strcmp(mode, "FAST_BLINK_GREEN")  == 0)
    setBlinkPattern(FAST_BLINK_GREEN);
  else if(strcmp(mode, "SLOW_BLINK_RED")    == 0)
    setBlinkPattern(SLOW_BLINK_RED);
  else if(strcmp(mode, "SLOW_BLINK_YELLOW") == 0)
    setBlinkPattern(SLOW_BLINK_YELLOW);
  else if(strcmp(mode, "SLOW_BLINK_GREEN")  == 0)
    setBlinkPattern(SLOW_BLINK_GREEN);
  else if(strcmp(mode, "ERROR_STATE")       == 0)
    setBlinkPattern(ERROR_STATE);
}

U32 lastMillis = 0;
U32 lastScanTime = 0;

U32 lastPubSubConnectAttempt = 0;
bool mqttServerConfigured = false;
bool mqttServerLookupError = false;

bool reportedResetReason = false;


void setupPubSubClient() {
  if(mqttServerConfigured || mqttServerLookupError)    // If we're already configured, or we've failed at attempting to do so, don't try again
    return;

  if(WiFi.status() != WL_CONNECTED)   // No point in doing anything here if we don't have internet access
    return;

  IPAddress serverIp(162,212,157,80);   // 162.212.157.80

  if(true || WiFi.hostByName(Eeprom.getMqttUrl(), serverIp)) {  //{P{P}}
    mqtt.mqttSetServer(serverIp, Eeprom.getMqttPort());
    mqttServerConfigured = true;
  } else {
    mqttServerLookupError = true;   // TODO: Try again in a few minutes
  }
}


U32 now_micros, now_millis;

bool serialSwapped = false;


void loopPubSub() {
  setupPubSubClient();

  if(!mqttServerConfigured)
    return;

  // Ensure constant contact with the mother ship
  if(!mqtt.mqttConnected()) {
    if(millis() - lastPubSubConnectAttempt > 5 * SECONDS) {
      reconnectToPubSubServer();      // Attempt to reconnect
      lastPubSubConnectAttempt = millis();
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
  mqtt.mqttSubscribe("v1/devices/me/attributes");                           // ... and subscribe to any shared attribute changes

  // Announce ourselves to the server
  mqtt.publishLocalCredentials(Eeprom.getLocalSsid(), Eeprom.getLocalPassword(), localAccessPointAddress);
  mqtt.publishSampleDuration(getSampleDuration());
  mqtt.publishTempSensorNameAndSoftwareVersion(getTemperatureSensorName(), FIRMWARE_VERSION);
  mqtt.publishStatusMessage("Connected");

  reportPlantowerSensorStatus();
  reportResetReason();
  reportCalibrationFactors();

  resetDataCollection();      // Now we can start our data collection efforts
  setBlinkPattern(SOLID_GREEN);
}



U32 plantowerPm1Sum = 0;
U32 plantowerPm25Sum = 0;
U32 plantowerPm10Sum = 0;
U16 plantowerSampleCount = 0;

#define BLINK_STATES 2
U8 blinkColor[BLINK_STATES] = { NONE, NONE };
U8 blinkTime = 24 * HOURS;
U32 blinkTimer = 0;
U8 blinkState = 0;

U32 lastReportTime = 0;
U32 samplingPeriodStartTime_micros;

bool initialConfigMode = false;


const char *defaultPingTargetHostName = "www.google.com";


const char *getWifiStatus() {
  return getWifiStatusName(WiFi.status());
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


const U32 MAX_COMMAND_LENGTH = 128;
String command;     // The command the user is composing during command mode

bool changedWifiCredentials = false;    // Track if we've changed wifi connection params during command mode

void setup() {

  Serial.begin(115200);

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


  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  strip.begin();
  Eeprom.begin();


  activateLed(RED);
  delay(300);
  activateLed(YELLOW);
  delay(300);
  activateLed(GREEN);
  delay(300);
  activateLed(NONE);

  handleUpgrade();

  Rest.variable("uptime",             &millis);
  Rest.variable("mqttServerConfigured",             &mqttServerConfigured);
  Rest.variable("mqttServerLookupError",            &mqttServerLookupError);
  Rest.variable("ledStyle",           &getLedStyle);
  Rest.variable("sensorsDetected",    &getSensorsDetected,    false);      // false ==> We'll handle quoting ourselves
  Rest.variable("plantowerDisabled",  &disablePlantower);      
  Rest.variable("calibrationFactors", &getCalibrationFactors, false);

  Rest.variable("lastReportTime",     &lastReportTime);
  Rest.variable("mqttStatus",         &getMqttStatus);
  Rest.variable("wifiStatus",         &getWifiStatus);

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

  Rest.function("restart",      rebootHandler);



  Rest.set_id("brdhse");  // Should be 9 chars or less
  Rest.set_name(Eeprom.getLocalSsid());

  WiFi.mode(WIFI_AP_STA);  

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);   // Disabling this allows us to connect via wifi AP without issue

  WiFi.begin();

  setupPubSubClient();
  mqtt.mqttSetCallback(messageReceivedFromMothership);

  paramManager.setLocalCredentialsChangedCallback([]()    { mqtt.publishLocalCredentials(Eeprom.getLocalSsid(), Eeprom.getLocalPassword(), localAccessPointAddress); });
  paramManager.setWifiCredentialsChangedCallback([]()     { changedWifiCredentials = true; });
  paramManager.setMqttCredentialsChangedCallback(onMqttPortUpdated);

  setupSensors();
 
  command.reserve(MAX_COMMAND_LENGTH);

  setupLocalAccessPoint(Eeprom.getLocalSsid(), Eeprom.getLocalPassword());

  // Flash yellow until we've connected via wifi
  setBlinkPattern(FAST_BLINK_YELLOW);


  server.begin();   // Start the web server
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


bool needToReconnectToWifi = false;
bool needToConnect = false;

bool isConnectingToWifi = false;    // True while a connection is in process

U32 wifiConnectStartTime;



void loop() {
  now_micros = micros();
  now_millis = millis();

  if(now_millis < lastMillis)
    millisOveflows++;
  
  lastMillis = now_millis;

  blink();    // Make the LEDs do their magic

  WiFiClient client = server.available();

  if(client) 
  {
    if(!serialSwapped)
      Serial.println("Handling incoming connection data");
    Rest.handle(client);
  }

  // Keep listening on the serial port until we switch it over to the PMS
  if(!serialSwapped) {
    Rest.handle(Serial);
    activatePlantower();  // This will have a little delay built in so that we can deactivate it if we're connected via serial
  }


  handleWifiConnection();
  loopPubSub();

  if(mqtt.mqttState() == MQTT_CONNECTED)
    loopSensors();
}


// For testing only
U32 blinkPatternTimer = 0;
int currPattern = 0;

void setBlinkPattern(BlinkPattern blinkPattern) {
  switch(blinkPattern) {
    case OFF:
      blinkColor[0] = NONE;
      blinkColor[1] = NONE;
      break;

    // Should do something different here!!!
    case STARTUP:
      blinkColor[0] = NONE;
      blinkColor[1] = NONE;
      break;

    case SOLID_RED:
    case SLOW_BLINK_RED:
    case FAST_BLINK_RED:
      blinkColor[0] = RED;
      blinkColor[1] = NONE;
      break;

    case SOLID_YELLOW:
    case SLOW_BLINK_YELLOW:
    case FAST_BLINK_YELLOW:
      blinkColor[0] = YELLOW;
      blinkColor[1] = NONE;
      break;

    case SOLID_GREEN:
    case SLOW_BLINK_GREEN:
    case FAST_BLINK_GREEN:
      blinkColor[0] = GREEN;
      blinkColor[1] = NONE;
      break;

    case ERROR_STATE:
      blinkColor[0] = RED;
      blinkColor[1] = YELLOW;
      break;
  }


  switch(blinkPattern) {
    case STARTUP:

      break;

    case OFF:
    case SOLID_RED:
    case SOLID_YELLOW:
    case SOLID_GREEN:
      blinkTime = 24 * HOURS;
      break;

    case SLOW_BLINK_RED:
    case SLOW_BLINK_YELLOW:
    case SLOW_BLINK_GREEN:
      blinkTime = 1 * SECONDS;
      break;

    case FAST_BLINK_RED:
    case FAST_BLINK_YELLOW:
    case FAST_BLINK_GREEN:
    case ERROR_STATE:
      blinkTime = 400 * MILLIS;
      break;
  }
}


void blink() {
  if(now_millis - blinkTimer < blinkTime)
    return;

  blinkTimer = now_millis;
  blinkState++;
  if(blinkState >= BLINK_STATES)
    blinkState = 0;

  activateLed(blinkColor[blinkState]);
}


bool getLowState() {
  return (Eeprom.getLedStyle() == ParameterManager::RYG_REVERSED) ? HIGH : LOW;
}

bool getHighState() {
  return (Eeprom.getLedStyle() == ParameterManager::RYG_REVERSED) ? LOW : HIGH;
}


void activateLed(U32 ledMask) {

  if(Eeprom.getLedStyle() == ParameterManager::RYG || Eeprom.getLedStyle() == ParameterManager::RYG_REVERSED) {

    digitalWrite(LED_RED,     (ledMask & RED)     ? getHighState() : getLowState());
    digitalWrite(LED_YELLOW,  (ledMask & YELLOW)  ? getHighState() : getLowState());
    digitalWrite(LED_GREEN,   (ledMask & GREEN)   ? getHighState() : getLowState());
    digitalWrite(LED_BUILTIN, (ledMask & BUILTIN) ? LOW : HIGH);    // builtin uses reverse states
  } 
  else if(Eeprom.getLedStyle() == ParameterManager::DOTSTAR) {

    int red   = (ledMask & (RED | YELLOW   )) ? 255 : 0;
    int green = (ledMask & (YELLOW | GREEN )) ? 255 : 0;
    int blue  = (ledMask & (0         )) ? 255 : 0;

    strip.setPixelColor(0, red, green, blue);
    strip.show(); 
  }
  else if(Eeprom.getLedStyle() == ParameterManager::FOUR_PIN_COMMON_ANNODE) {
    // Do something!
  }

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

  if(now_micros - samplingPeriodStartTime_micros > Eeprom.getSampleDuration() * SECONDS_TO_MICROS) {

    reportMeasurements();

    if(now_millis - lastScanTime > 30 * MINUTES) {
      scanVisibleNetworks();
    }

    if(now_millis - lastScanTime > 25 * SECONDS) {
      checkForFirmwareUpdates();
    }

    resetDataCollection();
  }

  else {

    if(serialSwapped && pms.read(PmsData)) {
      blinkTimer = now_millis + 1000;
      plantowerPm1Sum  += PmsData.PM_AE_UG_1_0;
      plantowerPm25Sum += PmsData.PM_AE_UG_2_5;
      plantowerPm10Sum += PmsData.PM_AE_UG_10_0;
      plantowerSampleCount++;
      plantowerSensorDetected = true;
    }
  }
}


void checkForFirmwareUpdates() {

  if(WiFi.status() != WL_CONNECTED)
    return;

  t_httpUpdate_return ret = ESPhttpUpdate.update(FIRMWARE_UPDATE_SERVER, FIRMWARE_UPDATE_PORT, "/update/", FIRMWARE_VERSION);

  switch(ret) {
    case HTTP_UPDATE_FAILED:
        break;
    case HTTP_UPDATE_NO_UPDATES:
        break;
    case HTTP_UPDATE_OK:    // Never get here because if there is an update, the update() function will take over and we'll reboot
        break;
  }
}


void resetDataCollection() {

  // Start the cycle anew
  samplingPeriodStartTime_micros = micros();
    
  // Reset Plantower data
  plantowerPm1Sum = 0;
  plantowerPm25Sum = 0;
  plantowerPm10Sum = 0;
  plantowerSampleCount = 0;
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


void reportCalibrationFactors() {
  mqtt.publishCalibrationFactors(getCalibrationFactors());
}



// Take any measurements we only do once per reporting period, and send all our data to the mothership
void reportMeasurements() {
    lastReportTime = millis();

    mqtt.publishDeviceData(millis(), ESP.getFreeHeap());

  if(plantowerSampleCount > 0) {
    F64 pm1  = (F64(plantowerPm1Sum) / (F64)plantowerSampleCount);
    F64 pm25 = (F64(plantowerPm25Sum) / (F64)plantowerSampleCount);
    F64 pm10 = (F64(plantowerPm10Sum) / (F64)plantowerSampleCount);
  
    mqtt.publishPmData(pm1, pm25, pm10, plantowerSampleCount);
    reportPlantowerSensorStatus();
  }

  if(BME_ok) {
    F32 pres, temp, hum;

    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);
    TemperatureSmoothingFilter.Filter(temp);

    mqtt.publishWeatherData(temp, TemperatureSmoothingFilter.Current(), hum, pres);
  }
}


// void processConfigCommand(const String &command) {
//   if(command == "uptime") {
//     if(millisOveflows > 0) {
//       //xx Serial.printf("%d*2^32 + ", millisOveflows);
//     }
//     //xx Serial.printf("%d seconds\n", millis() / SECONDS);
//   }
//   else if(command.startsWith("set wifi pw")) {
//     updateWifiPassword(&command.c_str()[12]);

//     //xx Serial.printf("Saved wifi pw: %s\n", wifiPassword, 123);
//   }
//   else if(command.startsWith("set local pw")) {
//     if(strlen(&command.c_str()[13]) < 8 || strlen(&command.c_str()[13]) > sizeof(localPassword) - 1) {
//       //xx Serial.printf("Password must be between at least 8 and %d characters long; not saving.\n", sizeof(localPassword) - 1);
//       return;
//     }

//     updateLocalPassword(&command.c_str()[13]);
//     //xx Serial.printf("Saved local pw: %s\n", localPassword);
//   }
//   else if(command.startsWith("set wifi ssid")) {
//     updateWifiSsid(&command.c_str()[14]);
//   }

//   #define COMMAND "use"
//   else if(command.startsWith(COMMAND)) {
//     int index = atoi(&command.c_str()[sizeof(COMMAND)]);
//     setWifiSsidFromScanResults(index);
//   }
//   else if(command.startsWith("set local ssid")) {
//     updateLocalSsid(&command.c_str()[15]);
//   }

//   else if(command.startsWith("set device token")) {
//     updateDeviceToken(&command.c_str()[17]);
//   }  
//   // else if(command.startsWith("set mqtt url")) {
//   //   updateMqttUrl(&command.c_str()[13]);
//   // }
//   else if(command.startsWith("set mqtt port")) {
//     updateMqttPort(&command.c_str()[14]);
//   }
//   else if (command.startsWith("set sample duration")) {
//     updateSampleDuration(&command.c_str()[20]);
//   }
//   else if(command.startsWith("con")) {
//     connectToWiFi(wifiSsid, wifiPassword, true);
//   }
//   else if(command.startsWith("cancel")) {
//     if(isConnectingToWifi) {
//       //xx Serial.println("\nCanceled connection attempt");
//       isConnectingToWifi = false;
//     }
//     else {
//       //xx Serial.println("No connection attempt in progress");
//     }

//   }
//   else if(command.startsWith("stat") || command.startsWith("show")) {
//     //xx Serial.println("\n====================================");
//     //xx Serial.println("Wifi Diagnostics:");
//     //xx WiFi.printDiag(Serial); 
//     //xx Serial.println("====================================");
//     //xx Serial.printf("Free sketch space: %d\n", ESP.getFreeSketchSpace());
//     //xx Serial.printf("Local ssid: %s\n", localSsid);
//     //xx Serial.printf("Local password: %s\n", localPassword);
//     //xx Serial.printf("MQTT url: %s\n", mqttUrl);
//     //xx Serial.printf("MQTT port: %d\n", mqttPort);
//     //xx Serial.printf("Device token: %s\n", deviceToken);
//     //xx Serial.printf("Temperature sensor: %s\n", BME_ok ? "OK" : "Not found");
//     //xx Serial.println("====================================");
//     //xx Serial.printf("Wifi status: %s\n",         getWifiStatusName(WiFi.status()));
//     //xx Serial.printf("MQTT status: %s\n", getSubPubStatusName(mqttState()));
//     //xx Serial.printf("Sampling duration: %d seconds   [set sample duration <n>]\n", sampleDuration);
//   }
//   else if(command.startsWith("scan")) {
//     scanVisibleNetworks();  
//   }
//   else if(command.startsWith("ping")) {
//     const int commandPrefixLen = strlen("PING ");
//     ping((command.length() > commandPrefixLen) ? &command.c_str()[commandPrefixLen] : defaultPingTargetHostName);
//   }
//   else {
//     //xx Serial.printf("Unknown command: %s\n", command.c_str());
//   }
// }


void printScanResult(U32 duration);     // Forward declare

void scanVisibleNetworks() {
  mqtt.publishStatusMessage("scanning");

  WiFi.scanNetworks(false, true);    // Include hidden access points


  U32 scanStartTime = millis();

  // Wait for scan to complete with max 30 seconds
  const int maxTime = 30;
  while(!WiFi.scanComplete()) { 
    if(millis() - scanStartTime > maxTime * SECONDS) {
      lastScanTime = millis();
      return;
    }
  }

  printScanResult(millis() - scanStartTime);

  lastScanTime = millis();
  resetDataCollection();
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
  if(WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    return;
  }

  if(WiFi.scanComplete() == WIFI_SCAN_FAILED) {
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


// Get a list of wifi hotspots the device can see
void printScanResult(U32 duration) {
  int networksFound = WiFi.scanComplete();

  mqtt.publishStatusMessage("scan results: " + String(networksFound) + " hotspots found in " + String(duration) + "ms");

  if(networksFound <= 0) {
    return;
  }

  for (int i = 0; i < networksFound; i++) {
    //xx Serial.printf("%d: %s <<%s>>, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i) == "" ? "[Hidden network]" : WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
  }

  // This is the format the Google Location services uses.  We'll create a list of these packets here so they can be passed through by the microservice
  // {
  //   "macAddress": "00:25:9c:cf:1c:ac",
  //   "signalStrength": -43,
  //   "age": 0,
  //   "channel": 11,
  //   "signalToNoiseRatio": 0
  // }

// TODO: Convert to arduinoJson
  String json = "{\"visibleHotspots\":\"["; 

  for (int i = 0; i < networksFound; i++) {
    json += "{\\\"macAddress\\\":\\\"" + WiFi.BSSIDstr(i) + "\\\",\\\"signalStrength\\\":" + String(WiFi.RSSI(i)) + 
                ", \\\"age\\\": 0, \\\"channel\\\":" + String(WiFi.channel(i)) + ",\\\"signalToNoiseRatio\\\": 0 }";
    if(i < networksFound - 1)
      json += ",";
  }
  json += "]\"}";

}


void ping(const char *target) {
  if(WiFi.status() != WL_CONNECTED)
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


U32 wifiConnectCooldownTimer = 0;

// Called from startup and loop
void handleWifiConnection() {

  // If our connection fails, let's take a brief break before trying again
  // Don't do anything during the cooldown period
  static const U32 WIFI_CONNECT_COOLDOWN_PERIOD = 5 * SECONDS;

  if(millis() - wifiConnectCooldownTimer < WIFI_CONNECT_COOLDOWN_PERIOD)
    return;

  // Handle a connection that is already underway:
  if(isConnectingToWifi) {
    if(WiFi.status() == WL_CONNECTED) {   // We just connected!  :-)

      onConnectedToWifi();
      isConnectingToWifi = false;
      return;
    }

    // Still working at it  :-(
    if(millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
      isConnectingToWifi = false;
      wifiConnectCooldownTimer = millis();
      if(!serialSwapped)
        Serial.printf("Failed to connect to wifi with credentials %s/%s at %d\n", Eeprom.getWifiSsid(), Eeprom.getWifiPassword(), millis());
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
  mqttServerLookupError = false;

  int status = WiFi.begin(Eeprom.getWifiSsid(), Eeprom.getWifiPassword());

  if(status != WL_CONNECT_FAILED) {   // OK
    isConnectingToWifi = true;
    wifiConnectStartTime = millis();    
  }
  else {                              // PROBLEM!
    wifiConnectCooldownTimer = millis();
    if(!serialSwapped)
      Serial.println("Attempt to connect to wifi failed with status WL_CONNECT_FAILED");
  }
}


U32 serialSwapTimer = 0;


// We just connected (or reconnected) to wifi
void onConnectedToWifi() {
  if(!serialSwapped)
    Serial.println("Connected to WiFi!");

  setBlinkPattern(FAST_BLINK_GREEN);   // Stop flashing yellow now that we've connected to wifi

  mqtt.publishLocalIp(WiFi.localIP());

  if(serialSwapTimer == 0)
    serialSwapTimer = millis();
}




void activatePlantower()
{
  if(serialSwapped)
    return;

  if(serialSwapTimer == 0)
    return;

  if(millis() - serialSwapTimer < 3 * SECONDS)
    return;

  if(disablePlantower)
    return;

  if(!serialSwapped) {  
    // Switch over to Plantower
    Serial.println("Turning over the serial port to the Plantower... no more messages here.");
    Serial.flush();   // Get any last bits out of there before switching the serial pins

    Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
    Serial.swap();    // D8 is now TX, D7 RX
    // Serial1.begin(115200);
    serialSwapped = true;
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

