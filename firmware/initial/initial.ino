#define NUMBER_VARIABLES 30


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <PubSubClient.h>       // For MQTT
#include <Dns.h>
#include <BME280I2C.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_DotStar.h>
#include <PMS.h>                // Plantower

#include "C:/dev/birdhouse/firmware/Types.h"
#include "C:/dev/birdhouse/firmware/Intervals.h"
#include "C:/dev/birdhouse/firmware/BirdhouseEeprom.h"    // For persisting values in EEPROM
#include "C:/dev/birdhouse/firmware/ESP8266Ping.h"        // For ping, of course



#include "c:/dev/aREST/aREST.h"              // Our REST server for provisioning
// #include "aREST2.h"



#define TEMPERATURE_UNIT BME280::TempUnit_Celsius
#define PRESSURE_UNIT    BME280::PresUnit_hPa


#define BME_SCL D5  // SPI (Clock)
#define BME_SDA D6  // SDA (Data) 

// Plantower Sensor pins are hardcoded below; they have to be on the serial pins

// Output LEDs
#define LED_BUILTIN D4
#define LED_GREEN   D0
#define LED_YELLOW  D1
#define LED_RED     D2
///// OR /////
#define LED_DATA_PIN  D1
#define LED_CLOCK_PIN D0


bool ledsInstalledBackwards = true;   // TODO --> Store in flash
bool traditionalLeds = false;         // TODO --> Store in flash


Adafruit_DotStar strip = Adafruit_DotStar(1, LED_DATA_PIN, LED_CLOCK_PIN, DOTSTAR_BGR);

//////////////////////
// WiFi Definitions //
//////////////////////


#define REST_LISTEN_PORT 80

aREST Rest = aREST();
WiFiServer server(REST_LISTEN_PORT);

#define PLANTOWER_SERIAL_BAUD_RATE 9600
#define SERIAL_BAUD_RATE 115200

// Plantower config
PMS pms(Serial);
PMS::DATA PmsData;

bool plantowerSensorDetected = false;
bool plantowerSensorDetectReported = false;
bool plantowerSensorNondetectReported = false;



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
  SOLID_RED,
  ALL_ON,
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


void activateLed(U32 ledMask);
BlinkPattern blinkPattern = STARTUP;


// U16 wifiChannel = 11;   // TODO: Delete? EEPROM, 0 = default, 1-13 are valid values


const U32 WIFI_CONNECT_TIMEOUT = 20 * SECONDS;

const char *localAccessPointAddress = "192.168.1.1";    // Url a user connected by wifi would use to access the device server
const char *localGatewayAddress = "192.168.1.2";

void connectToWiFi(bool); // Forward declare



U32 lastMillis = 0;
U32 lastScanTime = 0;

WiFiClient wfclient;
PubSubClient pubSubClient(wfclient);
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
    mqttSetServer(serverIp, Eeprom.getMqttPort());
    mqttServerConfigured = true;
  } else {
    mqttServerLookupError = true;   // TODO: Try again in a few minutes
  }
}


U32 pubSubConnectFailures = 0;
U32 now_micros, now_millis;

bool serialSwapped = false;


void loopPubSub() {
  setupPubSubClient();

  if(!mqttServerConfigured)
    return;

  // Ensure constant contact with the mother ship
  if(!mqttConnected()) {
    if (now_millis - lastPubSubConnectAttempt > 5 * SECONDS) {
      reconnectToPubSubServer();      // Attempt to reconnect
      lastPubSubConnectAttempt = now_millis;
      return;
    }
  }

  mqttLoop();
}


// Gets run when we're not connected to the PubSub client
void reconnectToPubSubServer() {
  if(!mqttServerConfigured)
    return;

  if(WiFi.status() != WL_CONNECTED)   // No point in doing anything here if we don't have internet access
    return;

  // Attempt to connect
  if (mqttConnect("Birdhouse", Eeprom.getDeviceToken(), "")) {   // ClientID, username, password
    onConnectedToPubSubServer();
  } else {    // Connection failed
    pubSubConnectFailures++;
  }
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


bool mqttLoop() {
  return pubSubClient.loop();
}


int mqttState() {
  return pubSubClient.state();
}


void mqttSetCallback(MQTT_CALLBACK_SIGNATURE) {
  pubSubClient.setCallback(callback);
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


bool mqttPublish(const char* topic, const char* payload) {
  return pubSubClient.publish(topic, payload, false);
}


bool mqttSubscribe(const char* topic) {
  return pubSubClient.subscribe(topic);
}


void onConnectedToPubSubServer() {
  mqttSubscribe("v1/devices/me/attributes");                           // ... and subscribe to any shared attribute changes

  // Announce ourselves to the server
  publishLocalCredentials();
  publishStatusMessage("Initialized");

  pubSubConnectFailures = 0;
}



#define MAX_BLINK_STATES 3
U8 blinkColor[MAX_BLINK_STATES] = { NONE, NONE, NONE };
U8 blinkTime = 24 * HOURS;
U32 blinkTimer = 0;
U8 blinkState = 0;
U8 blinkMode = 2;
U8 blinkStates = 1;
bool allLedsOn = false;

U32 lastReportTime = 0;
U32 samplingPeriodStartTime_micros;

bool initialConfigMode = false;


const char *defaultPingTargetHostName = "www.google.com";

bool doneSamplingTime() {
  return (now_micros - samplingPeriodStartTime_micros) > Eeprom.getSampleDuration() * SECONDS_TO_MICROS;
}

const char *getMqttStatus() {
  return getSubPubStatusName(mqttState());
}

const char *getWifiStatus() {
  return getWifiStatusName(WiFi.status());
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



  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  strip.begin();


  activateLed(RED);
  delay(500);
  activateLed(YELLOW);
  delay(500);
  activateLed(GREEN);
  delay(500);
  activateLed(NONE);

  Eeprom.begin();

  Rest.variable("uptime", &millis);
  Rest.variable("ledparams", &getLedParams, false);   // We'll handle quoting ourselves
  Rest.variable("sensorsDetected", &getSensorsDetected, false);
  Rest.variable("mqttStatus", &getMqttStatus);
  Rest.variable("wifiStatus", &getWifiStatus);
  Rest.variable("mqttServerConfigured", &mqttServerConfigured);
  Rest.variable("mqttServerLookupError", &mqttServerLookupError);


  Rest.variable("wifiScanResults", &getScanResults, false);
  Rest.variable("sampleDuration", &getSampleDuration);
  Rest.variable("deviceToken", &getDeviceToken);
  Rest.variable("localSsid", &getLocalSsid);
  Rest.variable("localPass", &getLocalPassword);
  Rest.variable("wifiSsid", &getWifiSsid);
  Rest.variable("wifiPass", &getWifiPassword);
  Rest.variable("mqttUrl", &getMqttUrl);
  Rest.variable("mqttPort", &getMqttPort);


  // These all take a single parameter specified on the cmd line
  Rest.function("localssid",    localSsidHandler);
  Rest.function("localpass",    localPasswordHandler);
  Rest.function("wifissid",     wifiSsidHandler);
  Rest.function("wifipass",     wifiPasswordHandler);
  Rest.function("devicetoken",  deviceTokenHandler);
  Rest.function("mqtturl",      mqttUrlHandler);
  Rest.function("mqttport",     mqttPortHandler);

  Rest.function("restart",      rebootHandler);
  Rest.function("ledson",       ledsOnHandler);
  Rest.function("ledsoff",      ledsOffHandler);



  Rest.set_id("brdhse");  // Should be 9 chars or less
  Rest.set_name(Eeprom.getLocalSsid());

  WiFi.mode(WIFI_AP_STA);  

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(true);


  setupPubSubClient();

  setupSensors();
 
  command.reserve(MAX_COMMAND_LENGTH);

  setupLocalAccessPoint(Eeprom.getLocalSsid(), Eeprom.getLocalPassword());
  connectToWiFi(changedWifiCredentials);

  activateLed(NONE);

  server.begin();
}


const char *getDeviceToken()   { return Eeprom.getDeviceToken();    }
const char *getLocalSsid()     { return Eeprom.getLocalSsid();      }
const char *getLocalPassword() { return Eeprom.getLocalPassword();  }
const char *getWifiSsid()      { return Eeprom.getWifiSsid();       }
const char *getWifiPassword()  { return Eeprom.getWifiPassword();   }
const char *getMqttUrl()       { return Eeprom.getMqttUrl();        }
U16 getSampleDuration()        { return Eeprom.getSampleDuration(); }
U16 getMqttPort()              { return Eeprom.getMqttPort();       }


void publishLocalCredentials() {
  StaticJsonBuffer<256> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["localSsid"] = Eeprom.getLocalSsid();
  root["localPassword"] = Eeprom.getLocalPassword();
  root["localIpAddress"] = localAccessPointAddress;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);
}


bool needToReconnectToWifi = false;
bool needToConnect = false;

// Called the very first time a Birdhouse is booted -- set defaults
void intitialConfig() {
  updateLocalPassword("88888888");
  updateLocalSsid("NewBirdhouse666");  
  updateWifiPassword("NOT_SET");
  updateWifiSsid("NOT_SET");  
  updateMqttUrl("www.sensorbot.org");
  updateMqttPort("1883");
  updateSampleDuration("30");
  updateDeviceToken("NOT_SET");

  Eeprom.writeSentinelMarker();

  initialConfigMode = true;
}


bool isConnectingToWifi = false;    // True while a connection is in process

U32 wifiConnectStartTime;



void loop() {
  now_micros = micros();
  now_millis = millis();

  lastMillis = now_millis;

  // advanceBlinkPattern();
  blink();

  WiFiClient client = server.available();
  if (client) {
    Rest.handle(client);
  }

  if(!serialSwapped)
    Rest.handle(Serial);


  loopPubSub();

  if(mqttState() == MQTT_CONNECTED)
    loopSensors();

  // checkForNewInputFromSerialPort();


  if(isConnectingToWifi) {
    connectingToWifi();
  }

// needToReconnectToWifi is never set to true...
  if(needToReconnectToWifi) {
    connectToWiFi(changedWifiCredentials);
    needToReconnectToWifi = false;
  }

}


// For testing only
U32 blinkPatternTimer = 0;
int currPattern = 0;

void advanceBlinkPattern() {

  if(now_millis > blinkPatternTimer) {
    blinkPatternTimer = now_millis + 5 * SECONDS;
    currPattern++;

    if(currPattern >= BLINK_PATTERN_COUNT)
      currPattern = 0;

    setBlinkMode(BlinkPattern(currPattern));
  }
}


void setBlinkMode(BlinkPattern blinkPattern) {
  switch(blinkPattern) {
    case OFF:
      blinkColor[0] = NONE;
      blinkStates = 1;
      break;

    case STARTUP:
      break;

    case ALL_ON:
      blinkColor[0] = RED;
      blinkColor[1] = YELLOW;
      blinkColor[2] = GREEN;
      blinkStates = 3;
      break;

    case SOLID_RED:
    case SLOW_BLINK_RED:
    case FAST_BLINK_RED:
      blinkColor[0] = RED;
      blinkStates = 1;
      break;

    case SOLID_YELLOW:
    case SLOW_BLINK_YELLOW:
    case FAST_BLINK_YELLOW:
      blinkColor[0] = YELLOW;
      blinkStates = 1;
      break;

    case SOLID_GREEN:
    case SLOW_BLINK_GREEN:
    case FAST_BLINK_GREEN:
      blinkColor[0] = GREEN;
      blinkStates = 1;
      break;

    case ERROR_STATE:
      blinkColor[0] = RED;
      blinkColor[1] = YELLOW;
      blinkStates = 2;
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

    case ALL_ON:
      blinkTime = 750 * MILLIS;
      break;
  }
}


void blink() {

  if(now_millis - blinkTimer < blinkTime)
    return;

  blinkTimer = now_millis;
  blinkState++;
  if(blinkState >= blinkStates)
    blinkState = 0;

  activateLed(blinkColor[blinkState]);
}


bool getLowState() {
  return ledsInstalledBackwards ? HIGH : LOW;
}

bool getHighState() {
  return ledsInstalledBackwards ? LOW : HIGH;
}


void activateLed(U32 ledMask) {

  if(traditionalLeds) {

    digitalWrite(LED_RED,     (ledMask & RED)     ? getHighState() : getLowState());
    digitalWrite(LED_YELLOW,  (ledMask & YELLOW)  ? getHighState() : getLowState());
    digitalWrite(LED_GREEN,   (ledMask & GREEN)   ? getHighState() : getLowState());
    digitalWrite(LED_BUILTIN, (ledMask & BUILTIN) ? LOW : HIGH);    // builtin uses reverse states

  } else {

    int red   = (ledMask & (RED | YELLOW   )) ? 255 : 0;
    int green = (ledMask & (YELLOW | GREEN )) ? 255 : 0;
    int blue  = (ledMask & (0         )) ? 255 : 0;

    strip.setPixelColor(0, red, green, blue);
    strip.show(); 
  }
}



int ledsOffHandler(String params) {
  setBlinkMode(OFF);
}


int ledsOnHandler(String params) {
  setBlinkMode(ALL_ON);
}


int rebootHandler(String params) {
  ESP.restart();
  return 1;
}

int localSsidHandler(String params) {
  updateLocalSsid(params.c_str());
  return 1;
}


int localPasswordHandler(String params) {
  if(strlen(params.c_str()) < 8 || strlen(params.c_str()) > Eeprom.getLocalPasswordSize() - 1)
    return 0;

  updateLocalPassword(params.c_str());

  return 1;
}


int wifiSsidHandler(String params) {
  updateWifiSsid(params.c_str());
  return 1;
}


int wifiPasswordHandler(String params) {
  updateWifiPassword(params.c_str());
  return 1;
}


int deviceTokenHandler(String params) {
  updateDeviceToken(params.c_str());
  return 1;
}


int mqttUrlHandler(String params) {
  updateMqttUrl(params.c_str());
  return 1;
}


int mqttPortHandler(String params) {
  updateMqttPort(params.c_str());
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


// Read sensors each loop tick
void loopSensors() {

  if(now_micros - samplingPeriodStartTime_micros > Eeprom.getSampleDuration() * SECONDS_TO_MICROS) {

    // If we overshot our sampling period slightly, compute a correction

    if(now_millis - lastScanTime > 30 * MINUTES) {
      scanVisibleNetworks();
    }
  }

  else {

    if(serialSwapped && pms.read(PmsData)) {
      blinkMode = 3;
      blinkTimer = now_millis + 1000;
      activateLed(GREEN);
      plantowerSensorDetected = true;
    }
  }
}


void reportLocalIp() {
  mqttPublishAttribute(String("{\"lanIpAddress\":\"") + WiFi.localIP().toString() + "\"}");
}


void processConfigCommand(const String &command) {
  if(command == "uptime") {
    //xx Serial.printf("%d seconds\n", millis() / SECONDS);
  }
  else if(command.startsWith("set wifi pw")) {
    updateWifiPassword(&command.c_str()[12]);

    //xx Serial.printf("Saved wifi pw: %s\n", wifiPassword, 123);
  }
  else if(command.startsWith("set local pw")) {
    if(strlen(&command.c_str()[13]) < 8 || strlen(&command.c_str()[13]) > Eeprom.getLocalPasswordSize() - 1) {
      //xx Serial.printf("Password must be between at least 8 and %d characters long; not saving.\n", sizeof(localPassword) - 1);
      return;
    }

    updateLocalPassword(&command.c_str()[13]);
    //xx Serial.printf("Saved local pw: %s\n", localPassword);
  }
  else if(command.startsWith("set wifi ssid")) {
    updateWifiSsid(&command.c_str()[14]);
  }

  #define COMMAND "use"
  else if(command.startsWith(COMMAND)) {
    int index = atoi(&command.c_str()[sizeof(COMMAND)]);
    setWifiSsidFromScanResults(index);
  }
  else if(command.startsWith("set local ssid")) {
    updateLocalSsid(&command.c_str()[15]);
  }

  else if(command.startsWith("set device token")) {
    updateDeviceToken(&command.c_str()[17]);
  }  
  // else if(command.startsWith("set mqtt url")) {
  //   updateMqttUrl(&command.c_str()[13]);
  // }
  else if(command.startsWith("set mqtt port")) {
    updateMqttPort(&command.c_str()[14]);
  }
  else if (command.startsWith("set sample duration")) {
    updateSampleDuration(&command.c_str()[20]);
  }
  else if(command.startsWith("con")) {
    connectToWiFi(true);
  }
  else if(command.startsWith("cancel")) {
    if(isConnectingToWifi) {
      //xx Serial.println("\nCanceled connection attempt");
      isConnectingToWifi = false;
    }
    else {
      //xx Serial.println("No connection attempt in progress");
    }

  }
  else if(command.startsWith("stat") || command.startsWith("show")) {
    //xx Serial.println("\n====================================");
    //xx Serial.println("Wifi Diagnostics:");
    //xx WiFi.printDiag(Serial); 
    //xx Serial.println("====================================");
    //xx Serial.printf("Free sketch space: %d\n", ESP.getFreeSketchSpace());
    //xx Serial.printf("Local ssid: %s\n", localSsid);
    //xx Serial.printf("Local password: %s\n", localPassword);
    //xx Serial.printf("MQTT url: %s\n", mqttUrl);
    //xx Serial.printf("MQTT port: %d\n", mqttPort);
    //xx Serial.printf("Device token: %s\n", deviceToken);
    //xx Serial.printf("Temperature sensor: %s\n", BME_ok ? "OK" : "Not found");
    //xx Serial.println("====================================");
    //xx Serial.printf("Wifi status: %s\n",         getWifiStatusName(WiFi.status()));
    //xx Serial.printf("MQTT status: %s\n", getSubPubStatusName(mqttState()));
    //xx Serial.printf("Sampling duration: %d seconds   [set sample duration <n>]\n", sampleDuration);
  }
  else if(command.startsWith("scan")) {
    scanVisibleNetworks();  
  }
  else if(command.startsWith("ping")) {
    const int commandPrefixLen = strlen("PING ");
    ping((command.length() > commandPrefixLen) ? &command.c_str()[commandPrefixLen] : defaultPingTargetHostName);
  }
  else {
    //xx Serial.printf("Unknown command: %s\n", command.c_str());
  }
}


void printScanResult(U32 duration);     // Forward declare

void scanVisibleNetworks() {
  publishStatusMessage("scanning");

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
}


void updateLocalSsid(const char *ssid) {
  Eeprom.setLocalSsid(ssid);
  publishLocalCredentials();
}


void updateLocalPassword(const char *password) {
  Eeprom.setLocalPassword(password);
  pubSubConnectFailures = 0;
  publishLocalCredentials();
}


void updateWifiSsid(const char *ssid) {
  Eeprom.setWifiSsid(ssid);
  changedWifiCredentials = true;
}


void updateWifiPassword(const char *password) {
  Eeprom.setWifiPassword(password);
  changedWifiCredentials = true;
  pubSubConnectFailures = 0;
  // initiateConnectionToWifi();
}


void updateMqttUrl(const char *url) {
  Eeprom.setMqttUrl(url);

  pubSubConnectFailures = 0;
  mqttDisconnect();
  mqttServerConfigured = false;
  mqttServerLookupError = false;

  setupPubSubClient();

  // Let's immediately connect our PubSub client
  reconnectToPubSubServer();
}


void updateDeviceToken(const char *token) {
  Eeprom.setDeviceToken(token);
  pubSubConnectFailures = 0;
}


void updateMqttPort(const char *port) {
  Eeprom.setMqttPort(port);

  if(Eeprom.getMqttPort() == 0)
    return;

  pubSubConnectFailures = 0;
  mqttDisconnect();
  mqttServerConfigured = false;
  mqttServerLookupError = false;

  setupPubSubClient();

  // Let's immediately connect our PubSub client
  reconnectToPubSubServer();
}


void updateSampleDuration(const char *duration) {
  Eeprom.setSampleDuration(duration);
}


void setWifiSsidFromScanResults(int index) {
  if(WiFi.scanComplete() == -1) {
    return;
  }

  if(WiFi.scanComplete() == -2) {
    return;
  }

  // Networks are 0-indexed, but user will be selecting a network based on 1-indexed display
  if(index < 1 || index > WiFi.scanComplete()) {
    return;
  }
  
  updateWifiSsid(WiFi.SSID(index - 1).c_str());
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


// Get a list of wifi hotspots the device can see
void printScanResult(U32 duration) {
  int networksFound = WiFi.scanComplete();

  publishStatusMessage("scan results: " + String(networksFound) + " hotspots found in " + String(duration) + "ms");

  if(networksFound <= 0) {
    return;
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

    WiFi.disconnect();
  }

  initiateConnectionToWifi();
}


void initiateConnectionToWifi()
{
  int status = WiFi.begin(Eeprom.getWifiSsid(), Eeprom.getWifiPassword());

  if (status != WL_CONNECT_FAILED) { 
    isConnectingToWifi = true;
    wifiConnectStartTime = millis();    
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

  if(millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
    isConnectingToWifi = false;
  }
}


// We just connected (or reconnected) to wifi
void onConnectedToWifi() {
  server.begin();

  reportLocalIp();

  // Switch over to Plantower
  Serial.println("Turning over the serial port to the Plantower... no more messages here.");
  Serial.flush();   // Get any last bits out of there before switching the serial pins


  if(!serialSwapped) {  
    Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
    Serial.swap();    // D8 is now TX, D7 RX
    // Serial1.begin(115200);
    serialSwapped = true;
  }
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
