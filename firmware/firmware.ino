// TODO: No AQ readings for 1 min after turned on (as per specs)
// TODO: No AQ measurements when humidity > 95% (as per specs)


#define NUMBER_VARIABLES 30


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <PubSubClient.h>       // For MQTT
#include <Dns.h>
#include <EEPROM.h>             // For persisting values in EEPROM
#include <BME280I2C.h>
#include <ArduinoJson.h>
#include <Wire.h>

#include <PMS.h>                // Plantower

// OTA Updates
#include <ArduinoOTA.h>
#include <ESP8266httpUpdate.h>

#include "c:/dev/aREST/aREST.h"              // Our REST server for provisioning
// #include "aREST2.h"

#include "ESP8266Ping.h"        // For ping, of course
#include "Filter.h"




#define FIRMWARE_VERSION "0.99" // Changing this variable name will require changing the build file to extract it properly
// Indulge me!
#define U8  uint8_t
#define S8  int8_t
#define S16 int16_t
#define U16 uint16_t
#define S32 int32_t 
#define U32 uint32            // unsigned long
#define F32 float
#define F64 double


// Interval definitions
#define SECONDS 1000
#define MINUTES 60 * SECONDS
#define HOURS 60 * MINUTES
#define DAYS 24 * HOURS
#define MILLIS_TO_MICROS 1000

#define TEMPERATURE_UNIT BME280::TempUnit_Celsius
#define PRESSURE_UNIT    BME280::PresUnit_hPa


// Define this to disable MQTT
// #define DISABLE_MQTT

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
// Temp sensor
#define BME_SCL D5//D1    // Green wire, SPI (Serial Clock)  5   // --> D5
#define BME_SDA D6//D2    // Blue wire,  SDA (Serial Data)   4  // --> D6

// Shinyei sensor
#define SHINYEI_SENSOR_DIGITAL_PIN_PM10 D3//D5 // "P2"
#define SHINYEI_SENSOR_DIGITAL_PIN_PM25 D3//D6 // "P1"

// Plantower Sensor pins are hardcoded below; they have to be on the serial pins

// Output LEDs
#define LED_BUILTIN D4
#define LED_GREEN D0      // --> D0
#define LED_YELLOW D1//D3     // --> D1
#define LED_RED D2//D4        // --> D2

bool ledsInstalledBackwards = true;   // TODO --> Store in flash

//////////////////////
// WiFi Definitions //
//////////////////////

// #define WEB_PORT 80
// ESP8266WebServer server(WEB_PORT);


#define REST_LISTEN_PORT 80

aREST Rest = aREST();
WiFiServer server(REST_LISTEN_PORT);

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

static const F64 Conc10InitialVal = 20;
static const F64 Conc25InitialVal = .05;
static const F64 Count10InitialVal = .5;
static const F64 Count25InitialVal = 0;

ExponentialFilter<F64> Conc10Filter1(3, Conc10InitialVal);
ExponentialFilter<F64> Conc10Filter2(5, Conc10InitialVal);
ExponentialFilter<F64> Conc10Filter3(10, Conc10InitialVal);
ExponentialFilter<F64> Conc10Filter4(20, Conc10InitialVal);

ExponentialFilter<F64> Conc25Filter1(3, Conc25InitialVal);
ExponentialFilter<F64> Conc25Filter2(5, Conc25InitialVal);
ExponentialFilter<F64> Conc25Filter3(10, Conc25InitialVal);
ExponentialFilter<F64> Conc25Filter4(20, Conc25InitialVal);


ExponentialFilter<F64> Count10Filter1(3, Count10InitialVal);
ExponentialFilter<F64> Count10Filter2(5, Count10InitialVal);
ExponentialFilter<F64> Count10Filter3(10, Count10InitialVal);
ExponentialFilter<F64> Count10Filter4(20, Count10InitialVal);

ExponentialFilter<F64> Count25Filter1(3, Count25InitialVal);
ExponentialFilter<F64> Count25Filter2(5, Count25InitialVal);
ExponentialFilter<F64> Count25Filter3(10, Count25InitialVal);
ExponentialFilter<F64> Count25Filter4(20, Count25InitialVal);




enum Leds {
  NONE = 0,
  RED = 1,
  YELLOW = 2,
  GREEN = 4,
  BUILTIN = 8
};


bool plantowerSensorDetected = false;
bool plantowerSensorDetectReported = false;
bool plantowerSensorNondetectReported = false;

U16 sampleDuration;     // In seconds
U32 sampleDuration_micros;

///// 
// For persisting values in EEPROM
const int SSID_LENGTH            = 32;
const int PASSWORD_LENGTH        = 63;
const int DEVICE_KEY_LENGTH      = 20;
const int URL_LENGTH             = 64;
const int SENTINEL_MARKER_LENGTH = 64;

// Our vars to hold the EEPROM values
char localSsid[SSID_LENGTH + 1];
char localPassword[PASSWORD_LENGTH + 1];
char wifiSsid[SSID_LENGTH + 1];
char wifiPassword[PASSWORD_LENGTH + 1];
char deviceToken[DEVICE_KEY_LENGTH + 1];
char mqttUrl[URL_LENGTH + 1];


const char SENTINEL_MARKER[SENTINEL_MARKER_LENGTH + 1] = "SensorBot by Chris Eykamp -- v106";   // Changing this will cause devices to revert to default configuration

U16 mqttPort;
U16 wifiChannel = 11;   // TODO: Delete? EEPROM, 0 = default, 1-13 are valid values

const int LOCAL_SSID_ADDRESS      = 0;
const int LOCAL_PASSWORD_ADDRESS  = LOCAL_SSID_ADDRESS      + sizeof(localSsid);
const int WIFI_SSID_ADDRESS       = LOCAL_PASSWORD_ADDRESS  + sizeof(localPassword);
const int WIFI_PASSWORD_ADDRESS   = WIFI_SSID_ADDRESS       + sizeof(wifiSsid);
const int DEVICE_KEY_ADDRESS      = WIFI_PASSWORD_ADDRESS   + sizeof(wifiPassword);
const int MQTT_URL_ADDRESS        = DEVICE_KEY_ADDRESS      + sizeof(deviceToken);
const int PUB_SUB_PORT_ADDRESS    = MQTT_URL_ADDRESS        + sizeof(mqttUrl);
const int SAMPLE_DURATION_ADDRESS = PUB_SUB_PORT_ADDRESS    + sizeof(mqttPort);
const int SENTINEL_ADDRESS        = SAMPLE_DURATION_ADDRESS + sizeof(sampleDuration);
const int NEXT_ADDRESS            = SENTINEL_ADDRESS        + sizeof(SENTINEL_MARKER); 

const int EEPROM_SIZE = NEXT_ADDRESS;

// U8 macAddress[WL_MAC_ADDR_LENGTH];
// WiFi.softAPmacAddress(macAddress);

U32 millisOveflows = 0;

const U32 WIFI_CONNECT_TIMEOUT = 20 * SECONDS;

const char *localAccessPointAddress = "192.168.1.1";    // Url a user connected by wifi would use to access the device server
const char *localGatewayAddress = "192.168.1.2";

void connectToWiFi(const char*, const char*, bool); // Forward declare
void activateLed(U32 ledMask);


void messageReceivedFromMothership(char* topic, byte* payload, unsigned int length) {
  //xx Serial.printf("Message arrived [%s]\n", topic);

  // See https://github.com/bblanchon/ArduinoJson for usage
  // StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
  // JsonObject &root = jsonBuffer.parseObject(payload);

  // // const char *test = root["mqttServer"];
  // // if(strcmp(test, "") != 0 ) {
  // //   publishStatusMessage(String("Got ") +String(test));
  // // }

  // const char *color = root["LED"];

  // if(strcmp(color, "GREEN") == 0)
  //   activateLed(GREEN);
  // else if(strcmp(color, "YELLOW") == 0)
  //   activateLed(YELLOW);
  // else if(strcmp(color, "RED") == 0)
  //   activateLed(RED);
  // else
  //   activateLed(NONE);
}

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

  if(WiFi.hostByName(mqttUrl, serverIp)) {
    mqttSetServer(serverIp, mqttPort);
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
  if (mqttConnect("Birdhouse", deviceToken, "")) {   // ClientID, username, password
    onConnectedToPubSubServer();
  } else {    // Connection failed
    pubSubConnectFailures++;
  }
}



void mqttSetServer(IPAddress &ip, uint16_t port) {
# ifdef DISABLE_MQTT
    //xx Serial.printf("MQTT SET SERVER: %s | %d\n", ip.toString().c_str(), port);
    return;
# else
    pubSubClient.setServer(ip, port);
# endif
}


bool mqttConnect(const char *id, const char *user, const char *pass) {
# ifdef DISABLE_MQTT
    //xx Serial.printf("MQTT CON: %s | %s | %s\n", id, user, pass);
    return true;
# else
    return pubSubClient.connect(id, user, pass);
# endif
}


bool mqttConnected() {
# ifdef DISABLE_MQTT
    return true;
# else
    return pubSubClient.connected();
# endif
}


void mqttDisconnect() {
# ifdef DISABLE_MQTT
    //xx Serial.printf("MQTT DIS\n");
# else
    pubSubClient.disconnect();
# endif
}


bool mqttLoop() {
# ifdef DISABLE_MQTT
    return true;
# else
    return pubSubClient.loop();
# endif
}


int mqttState() {
# ifdef DISABLE_MQTT
    return MQTT_CONNECTED;
# else
    return pubSubClient.state();
# endif
}


void mqttSetCallback(MQTT_CALLBACK_SIGNATURE) {
# ifdef DISABLE_MQTT
    //xx Serial.printf("MQTT SETTING CALLBACK\n");
# else
    pubSubClient.setCallback(callback);
# endif
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
# ifdef DISABLE_MQTT
    //xx Serial.printf("MQTT MSG: %s | %s\n", topic, payload);
    return true;
# else
    return pubSubClient.publish(topic, payload, false);
# endif
}


bool mqttSubscribe(const char* topic) {
# ifdef DISABLE_MQTT
    //xx Serial.printf("MQTT SUB: %s\n", topic);
    return true;
# else
    return pubSubClient.subscribe(topic);
# endif
}


void onConnectedToPubSubServer() {
  //xx Serial.println("Connected to MQTT server");                          // Even if we're not in verbose mode... Victories are to be celebrated!
  mqttSubscribe("v1/devices/me/attributes");                           // ... and subscribe to any shared attribute changes

  // Announce ourselves to the server
  publishLocalCredentials();
  publishSampleDuration();
  publishTempSensorNameAndSoftwareVersion();
  publishStatusMessage("Connected");

  reportPlantowerSensorStatus();
  reportResetReason();

  pubSubConnectFailures = 0;

  //xx Serial.println("Starting data collection");
  resetDataCollection();      // Now we can start our data collection efforts
}



U32 plantowerPm1Sum = 0;
U32 plantowerPm25Sum = 0;
U32 plantowerPm10Sum = 0;
int plantowerSampleCount = 0;

U32 blinkTimer = 0;
U8 blinkState = 0;
U8 blinkMode = 0;

U32 lastReportTime = 0;
U32 samplingPeriodStartTime_micros;

bool initialConfigMode = false;


const char *defaultPingTargetHostName = "www.google.com";

bool doneSamplingTime() {
  return (now_micros - samplingPeriodStartTime_micros) > sampleDuration_micros;
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


# ifdef DISABLE_MQTT
    //xx Serial.println("\n\n*** MQTT FUNCTIONALITY DISABLED IN THIS BUILD! ***\n");
# endif

  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);


  activateLed(RED);
  delay(500);
  activateLed(YELLOW);
  delay(500);
  activateLed(GREEN);
  delay(500);
  activateLed(NONE);

  EEPROM.begin(EEPROM_SIZE);

  verifySentinelMarker();

  readStringFromEeprom(LOCAL_SSID_ADDRESS,     sizeof(localSsid)     - 1, localSsid);
  readStringFromEeprom(LOCAL_PASSWORD_ADDRESS, sizeof(localPassword) - 1, localPassword);
  readStringFromEeprom(WIFI_SSID_ADDRESS,      sizeof(wifiSsid)      - 1, wifiSsid);
  readStringFromEeprom(WIFI_PASSWORD_ADDRESS,  sizeof(wifiPassword)  - 1, wifiPassword);
  readStringFromEeprom(DEVICE_KEY_ADDRESS,     sizeof(deviceToken)   - 1, deviceToken);
  readStringFromEeprom(MQTT_URL_ADDRESS,       sizeof(mqttUrl)       - 1, mqttUrl);

  mqttPort = EepromReadU16(PUB_SUB_PORT_ADDRESS);
  setSampleDuration(EepromReadU16(SAMPLE_DURATION_ADDRESS));

  Serial.printf("Local SSID: %s\n", localSsid);
  Serial.printf("Local PW: %s\n", localPassword);
  Serial.printf("WiFi SSID: %s\n", wifiSsid);
  Serial.printf("WiFi PW: %s\n", wifiPassword);
  Serial.printf("Device Key: %s\n", deviceToken);
  Serial.printf("MQTT URL: %s\n", mqttUrl);
  Serial.printf("MQTT Port: %d\n", mqttPort);
  Serial.printf("Sample duration: %d sec\n", sampleDuration);


  Rest.variable("uptime", &millis);
  Rest.variable("lastReportTime", &lastReportTime);
  Rest.variable("plantowerSensorDetected", &plantowerSensorDetected);
  Rest.variable("now_micros", &now_micros);
  Rest.variable("samplingPeriodStartTime_micros", &samplingPeriodStartTime_micros);
  Rest.variable("sampleDuration_micros", &sampleDuration_micros);
  Rest.variable("mqttStatus", &getMqttStatus);
  Rest.variable("wifiStatus", &getWifiStatus);


  Rest.variable("doneSamplingTime", &doneSamplingTime);
  // Rest.variable("firmwareVersion", &F(FIRMWARE_VERSION));




  Rest.variable("sampleCount", &plantowerSampleCount);
  Rest.variable("deviceToken", &deviceToken);
  Rest.variable("localSsid", &localSsid);
  Rest.variable("localPass", &localPassword);
  Rest.variable("wifiSsid", &wifiSsid);
  Rest.variable("wifiPass", &wifiPassword);
  Rest.variable("mqttUrl", &mqttUrl);
  Rest.variable("mqttPort", &mqttPort);

  // Delete these
  // Rest.variable("concval", &Conc10InitialVal);  //F64
  // Rest.variable("lastMillis", &lastMillis);  //U32



  // These all take a single parameter specified on the cmd line
  Rest.function("localssid",    localSsidHandler);
  Rest.function("localpass",    localPasswordHandler);
  Rest.function("wifissid",     wifiSsidHandler);
  Rest.function("wifipass",     wifiPasswordHandler);
  Rest.function("devicetoken",  deviceTokenHandler);
  Rest.function("mqtturl",      mqttUrlHandler);
  Rest.function("mqttport",     mqttPortHandler);

  Rest.function("restart",      rebootHandler);



  Rest.set_id("brdhse");  // Should be 9 chars or less
  Rest.set_name(localSsid);



  WiFi.mode(WIFI_AP_STA);  

  // We'll handle these ourselves
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(true);


  setupPubSubClient();
  // mqttSetCallback(messageReceivedFromMothership);


  setupSensors();
 
  // setupWebHandlers();
  
  command.reserve(MAX_COMMAND_LENGTH);

  setupLocalAccessPoint(localSsid, localPassword);
  connectToWiFi(wifiSsid, wifiPassword, changedWifiCredentials);

  setupOta();

  activateLed(NONE);

  server.begin();
}


void publishSampleDuration() {
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["sampleDuration"] = sampleDuration;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);  
}


void publishLocalCredentials() {
  StaticJsonBuffer<256> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["localSsid"] = localSsid;
  root["localPassword"] = localPassword;
  root["localIpAddress"] = localAccessPointAddress;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);
}


void publishTempSensorNameAndSoftwareVersion() {
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["temperatureSensor"] = getTemperatureSensorName();
  root["firmwareVersion"] = FIRMWARE_VERSION;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json);
}


bool getLowState() {
  return ledsInstalledBackwards ? HIGH : LOW;
}

bool getHighState() {
  return ledsInstalledBackwards ? LOW : HIGH;
}

void activateLed(U32 ledMask) {
  digitalWrite(LED_RED, (ledMask & RED) ? getHighState() : getLowState());
  digitalWrite(LED_YELLOW, (ledMask & YELLOW) ? getHighState() : getLowState());
  digitalWrite(LED_GREEN, (ledMask & GREEN) ? getHighState() : getLowState());
  digitalWrite(LED_BUILTIN, (ledMask & BUILTIN) ? LOW : HIGH);
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

  writeStringToEeprom(SENTINEL_ADDRESS, sizeof(SENTINEL_MARKER) - 1, SENTINEL_MARKER);

  initialConfigMode = true;
}

// Checks if this Birdhouse has ever been booted before
void verifySentinelMarker() {

  char storedSentinelMarker[SENTINEL_MARKER_LENGTH + 1];
  readStringFromEeprom(SENTINEL_ADDRESS, sizeof(storedSentinelMarker) - 1, storedSentinelMarker);

  // Sentinel is missing!  This is our very first boot.
  if(strcmp(SENTINEL_MARKER, storedSentinelMarker) != 0)
    intitialConfig();
}


bool isConnectingToWifi = false;    // True while a connection is in process

U32 connectingToWifiDotTimer;
U32 wifiConnectStartTime;



void loop() {
  now_micros = micros();
  now_millis = millis();

  if(now_millis < lastMillis)
    millisOveflows++;
  
  lastMillis = now_millis;

  blink();

  U32 i = 0;
  WiFiClient client = server.available();
  if (client) {
    // blinkMode = 3;
    // blinkTimer = now_millis + 1000;
    while(client.connected() && !client.available() && i < 1000) {
      delay(1);
      i++;
    }
    Rest.handle(client);
  }

  loopPubSub();


#ifdef DISABLE_MQTT
  bool doLoop = true;
#else
  bool doLoop = mqttState() == MQTT_CONNECTED;
#endif

  if(doLoop)
    loopSensors();

  // checkForNewInputFromSerialPort();


  if(isConnectingToWifi) {
    connectingToWifi();
  }

  if(needToReconnectToWifi) {
    connectToWiFi(wifiSsid, wifiPassword, changedWifiCredentials);
    needToReconnectToWifi = false;
  }

}


void blink() {
  if(blinkMode == 0)
    return;

  if(blinkMode == 1) {
    if(now_millis > blinkTimer) {
      blinkTimer = now_millis + 500;
      blinkState = !blinkState;

      activateLed(blinkState ? RED | GREEN : YELLOW);
    }
  }

  else if (blinkMode == 2) {    // Chase
    if(now_millis > blinkTimer)
    {
      blinkTimer = now_millis + 100;
      blinkState++;
      if(blinkState >= 3)
        blinkState = 0;
      // digitalWrite(LED_BUILTIN, blinkState == 0 ? HIGH : LOW);

      if(blinkState == 0)
        activateLed(GREEN);
      else if (blinkState == 1)
        activateLed(YELLOW);
      else
        activateLed(RED);
    }
  }

  else if(blinkMode == 3) {  // Turn off
    if(now_millis > blinkTimer) {
      activateLed(NONE);
    }
  }
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
  if(strlen(params.c_str()) < 8 || strlen(params.c_str()) > sizeof(localPassword) - 1)
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
  return updateMqttPort(params.c_str());
}


bool BME_ok = false;


// BME280I2C::Settings settings(
//    BME280::OSR_X8,    // Temp   --> default was OSR_1X
//    BME280::OSR_X8,    // Humid  --> default was OSR_1X
//    BME280::OSR_X1,    // Press
//    BME280::Mode_Forced,   // Power mode
//    BME280::StandbyTime_1000ms, 
//    BME280::Filter_Off,    // Pressure filter
//    BME280::SpiEnable_False,
//    0x76 // I2C address. I2C specific.
// );

// Temperature sensor
BME280I2C bme;


// Orange: VCC, yellow: GND, Green: SCL, d1, Blue: SDA, d2
void setupSensors() {
  // Serial.println(getResetReason());


  // Temperature, humidity, and barometric pressure
  Wire.begin(BME_SDA, BME_SCL);
  BME_ok = bme.begin();

  if(BME_ok) {
    F32 t = bme.temp();

    TemperatureSmoothingFilter.SetCurrent(t);

    //xx Serial.printf("Temperature sensor: %s\n", getTemperatureSensorName());

    char str_temp[6];
    dtostrf(t, 4, 2, str_temp);

    //xx Serial.printf("Initializing temperature filter %s\n", str_temp);
  }


  pinMode(SHINYEI_SENSOR_DIGITAL_PIN_PM10, INPUT);
  pinMode(SHINYEI_SENSOR_DIGITAL_PIN_PM25, INPUT);

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


bool triggerP1, triggerP2;
U32 durationP1, durationP2;
U32 triggerOnP1, triggerOnP2;

U32 shinyeiLogicReads = 0;

// Read sensors each loop tick
void loopSensors() {

  // Collect Shinyei data every loop
  bool valP1 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM10);
  bool valP2 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM25);

  shinyeiLogicReads++;

  if(doneSamplingTime()) {
    // If we overshot our sampling period slightly, compute a correction
    U32 overage = (now_micros - samplingPeriodStartTime_micros) - sampleDuration_micros;

    if(valP1 == LOW) {
      durationP1 += now_micros - triggerOnP1 - overage;
    }
    if(valP2 == LOW) {
      durationP2 += now_micros - triggerOnP2 - overage;
    }

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

    // Track the duration each pin is LOW  
    // Going low... start timer
    if(valP1 == LOW && triggerP1 == LOW) {
      triggerP1 = HIGH;
      triggerOnP1 = now_micros;
    }
    
    // Going high... stop timer
    else if (valP1 == HIGH && triggerP1 == HIGH) {
      U32 pulseLengthP1 = now_micros - triggerOnP1;
      durationP1 += pulseLengthP1;
      triggerP1 = LOW;
    }
    
    // Going low... start timer
    if(valP2 == LOW && triggerP2 == LOW) {
      triggerP2 = HIGH;
      triggerOnP2 = now_micros;
    }
    
    // Going high... stop timer
    else if (valP2 == HIGH && triggerP2 == HIGH) {
      U32 pulseLengthP2 = now_micros - triggerOnP2;
      durationP2 += pulseLengthP2;
      triggerP2 = LOW;
    }


    if(pms.read(PmsData)) {
      blinkMode = 3;
      blinkTimer = now_millis + 1000;
      activateLed(GREEN);
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


//   t_httpUpdate_return ret = ESPhttpUpdate.update(FIRMWARE_UPDATE_SERVER, FIRMWARE_UPDATE_PORT, String("/update/?mqtt_status=")+getMqttStatus(), FIRMWARE_VERSION);
  String path = String("/update/") + getSubPubStatusName(mqttState()) + String("/");

  t_httpUpdate_return ret = ESPhttpUpdate.update(FIRMWARE_UPDATE_SERVER, FIRMWARE_UPDATE_PORT, path.c_str(), FIRMWARE_VERSION);

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

  bool valP1 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM10);
  bool valP2 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM25);

  durationP1 = 0;
  durationP2 = 0;
  shinyeiLogicReads = 0;

  samplingPeriodStartTime_micros = micros();
  triggerOnP1 = samplingPeriodStartTime_micros;
  triggerOnP2 = samplingPeriodStartTime_micros;
    
  // We want to trigger when the Shinyei pins change state from what we just read
  triggerP1 = !valP1;
  triggerP2 = !valP2;

  // Reset Plantower data
  plantowerPm1Sum = 0;
  plantowerPm25Sum = 0;
  plantowerPm10Sum = 0;
  plantowerSampleCount = 0;


 //xx Serial.printf("P1 / P2 starting %s / %s\n", valP1 ? "HIGH" : "LOW",valP2 ? "HIGH" : "LOW");
}



// Generates PM10 and PM2.5 count from LPO.
// Derived from code created by Chris Nafis from graph provided by Shinyei
// http://www.howmuchsnow.com/arduino/airquality/grovedust/
// ratio is a number between 0 and 100
F64 lpoToParticleCount(F64 ratio) { 
  return 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62;  // Particles per .01 ft^3
}

F64 sphericalVolume(F64 radius) {
  static const F64 pi = 3.14159;
  return (4.0 / 3.0) * pi * pow(radius, 3);
}


void reportPlantowerDetectNondetect(bool sensorFound) {
  mqttPublishAttribute(String("{\"plantowerSensorDetected\":") + (sensorFound ? "True" : "False") + "}");
}


void reportPlantowerSensorStatus() {
  // Report each status only once
  if(!plantowerSensorDetected && !plantowerSensorNondetectReported) {
    reportPlantowerDetectNondetect(false);
    plantowerSensorNondetectReported = true;
  }

  if(plantowerSensorDetected && !plantowerSensorDetectReported) {
    reportPlantowerDetectNondetect(true);
    plantowerSensorDetectReported = true;
  }
}


void reportResetReason() {
  if(reportedResetReason)
    return;

  mqttPublishAttribute(String("{\"lastResetReason\":\"") + ESP.getResetReason() + "\"}");
  reportedResetReason = true;
}



// Take any measurements we only do once per reporting period, and send all our data to the mothership
void reportMeasurements() {
    activateLed(YELLOW);
    lastReportTime = millis();
    // Function creates particle count and mass concentration
    // from PPD-42 low pulse occupancy (LPO).


  //xx Serial.printf("Durations (10/2.5): %dµs / %dµs   %s\n", durationP1, durationP2, (durationP1 > sampleDuration_micros || 
                                                                                   // durationP2 > sampleDuration_micros) ? "ERROR" : "");
      
      //               microseconds            microseconds           
      F64 ratioP1 = durationP1 / ((F64)sampleDuration_micros) * 100.0;    // Generate a percentage expressed as an integer between 0 and 100
      F64 ratioP2 = durationP2 / ((F64)sampleDuration_micros) * 100.0;

//xx Serial.printf("10/2.5 ratios: %s% / %s%\n", String(ratioP1).c_str(), String(ratioP2).c_str());

      F64 countP1 = lpoToParticleCount(ratioP1);  // Particles / .01 ft^3
      F64 countP2 = lpoToParticleCount(ratioP2);  // Particles / .01 ft^3

      F64 PM10count = countP1;
      F64 PM25count = countP2 - countP1;
      
      // Assumes density, shape, and size of dust
      // to estimate mass concentration from particle count.
      // This method was described in a 2009 paper
      // Preliminary Screening System for Ambient Air Quality in Southeast Philadelphia by Uva, M., Falcone, R., McClellan, A., and Ostapowicz, E.
      // http://www.cleanair.org/sites/default/files/Drexel%20Air%20Monitoring_-_Final_Report_-_Team_19_0.pdf
      // http://www.eunetair.it/cost/meetings/Istanbul/01-PRESENTATIONS/06_WG3-WG4-SESSION_WG3/04_ISTANBUL_WG-MC-MEETING_Jovasevic-Stojanovic.pdf
      //
      // This method does not use the correction factors, based on the presence of humidity and rain in the paper.
      //
      // convert from particles/0.01 ft3 to μg/m3
      static const F64 K = 3531.5; // .01 ft^3 / m^3    
      static const F64 density = 1.65 * pow(10, 12);   // All particles assumed spherical, with a density of 1.65E12 μg/m^3 (from paper)

      // PM10 mass concentration algorithm
      static const F64 largeParticleRadius = 2.6 * pow(10, -6);     // The radius of a particle in the channel >2.5 μm is 2.60 μm (from paper)
      static const F64 mass10 = density * sphericalVolume(largeParticleRadius);     // μg/particle
      F64 PM10conc = PM10count * K * mass10;    // μg/m^3
      
      // PM2.5 mass concentration algorithm
      static const F64 smallParticleRadius = 0.44 * pow(10, -6);    // The radius of a particle in the channel <2.5 μm is 0.44 μm (from paper)
      static const F64 mass25 = density * sphericalVolume(smallParticleRadius);   // μg/particle
      F64 PM25conc = PM25count * K * mass25;    // μg/m^3


      Conc10Filter1.Filter(PM10conc);
      Conc10Filter2.Filter(PM10conc);
      Conc10Filter3.Filter(PM10conc);
      Conc10Filter4.Filter(PM10conc);

      Conc25Filter1.Filter(PM25conc);
      Conc25Filter2.Filter(PM25conc);
      Conc25Filter3.Filter(PM25conc);
      Conc25Filter4.Filter(PM25conc);
      
      Count10Filter1.Filter(PM10count);
      Count10Filter2.Filter(PM10count);
      Count10Filter3.Filter(PM10count);
      Count10Filter4.Filter(PM10count);
      
      Count25Filter1.Filter(PM25count);
      Count25Filter2.Filter(PM25count);
      Count25Filter3.Filter(PM25count);
      Count25Filter4.Filter(PM25count);
      
      // StaticJsonBuffer<1024> jsonBuffer;
      // JsonObject &root = jsonBuffer.createObject();

      // root["shinyeiPM10conc"] = PM10conc;
      // root["shinyeiPM10ratio"] = ratioP1;
      // root["shinyeiPM10mass"] = mass10;
      // root["shinyeiPM10duration"] = durationP1;
      // root["shinyeiPM10count"] = PM10count;
      // root["shinyeiPM25conc"] = PM25conc;
      // root["shinyeiPM25ratio"] = ratioP2;
      // root["shinyeiPM25mass"] = mass25;
      // root["shinyeiPM25duration"] = durationP2;
      // root["shinyeiPM25count"] = PM25count;
      // root["ashinyeiPM10conc"] = Conc10Filter1.Current();
      // root["bshinyeiPM10conc"] = Conc10Filter2.Current();
      // root["cshinyeiPM10conc"] = Conc10Filter3.Current();
      // root["dshinyeiPM10conc"] = Conc10Filter4.Current();
      // root["ashinyeiPM25conc"] = Conc25Filter1.Current();
      // root["bshinyeiPM25conc"] = Conc25Filter2.Current();
      // root["cshinyeiPM25conc"] = Conc25Filter3.Current();
      // root["dshinyeiPM25conc"] = Conc25Filter4.Current();
      // root["ashinyeiPM10count"] = Count10Filter1.Current();
      // root["bshinyeiPM10count"] = Count10Filter2.Current();
      // root["cshinyeiPM10count"] = Count10Filter3.Current();
      // root["dshinyeiPM10count"] = Count10Filter4.Current();
      // root["ashinyeiPM25count"] = Count25Filter1.Current();
      // root["bshinyeiPM25count"] = Count25Filter2.Current();
      // root["cshinyeiPM25count"] = Count25Filter3.Current();
      // root["dshinyeiPM25count"] = Count25Filter4.Current();

      // String json;
      // root.printTo(json);
      // Serial.printf("json: %s\n",json.c_str());

      // bool ok = mqttPublishTelemetry(json);  

      String json = String("{") +
      "\"uptime\":"              + String(millis()) + "," + 
      "\"freeHeap\":"            + String(ESP.getFreeHeap()) + "," + 
      "\"shinyeiLogicGoodReads\":"   + String(shinyeiLogicReads) + "," + 

      "\"shinyeiPM10conc\":"     + String(PM10conc) + "," + 
      "\"shinyeiPM10ratio\":"    + String(ratioP1) + "," + 
      "\"shinyeiPM10mass\":"     + String(mass10) + "," + 
      "\"shinyeiPM10duration\":" + String(durationP1) + "," + 
      "\"shinyeiPM10count\":"    + String(PM10count) + "," + 
      "\"shinyeiPM25conc\":"     + String(PM25conc) + "," + 
      "\"shinyeiPM25ratio\":"    + String(ratioP2) + "," + 
      "\"shinyeiPM25mass\":"     + String(mass25) + "," + 
      "\"shinyeiPM25duration\":" + String(durationP2) + "," + 
      "\"shinyeiPM25count\":"    + String(PM25count) + "," +
      "\"ashinyeiPM10conc\":"    + String(Conc10Filter1.Current()) + "," + 
      "\"bshinyeiPM10conc\":"    + String(Conc10Filter2.Current()) + "," + 
      "\"cshinyeiPM10conc\":"    + String(Conc10Filter3.Current()) + "," + 
      "\"dshinyeiPM10conc\":"    + String(Conc10Filter4.Current()) + "," + 
      "\"ashinyeiPM25conc\":"    + String(Conc25Filter1.Current()) + "," + 
      "\"bshinyeiPM25conc\":"    + String(Conc25Filter2.Current()) + "," + 
      "\"cshinyeiPM25conc\":"    + String(Conc25Filter3.Current()) + "," + 
      "\"dshinyeiPM25conc\":"    + String(Conc25Filter4.Current()) + "," + 
      "\"ashinyeiPM10count\":"   + String(Count10Filter1.Current()) + "," + 
      "\"bshinyeiPM10count\":"   + String(Count10Filter2.Current()) + "," + 
      "\"cshinyeiPM10count\":"   + String(Count10Filter3.Current()) + "," + 
      "\"dshinyeiPM10count\":"   + String(Count10Filter4.Current()) + "," + 
      "\"ashinyeiPM25count\":"   + String(Count25Filter1.Current()) + "," + 
      "\"bshinyeiPM25count\":"   + String(Count25Filter2.Current()) + "," + 
      "\"cshinyeiPM25count\":"   + String(Count25Filter3.Current()) + "," + 
      "\"dshinyeiPM25count\":"   + String(Count25Filter4.Current()) + " } ";


      //xx Serial.println(json);

      U32 timenow = millis();

      if(!mqttPublishTelemetry(json)) {
        //xx Serial.printf("Could not publish Shinyei PM data: %s\n", json.c_str());
        //xx Serial.printf("MQTT Status: %s\n", String(getSubPubStatusName(mqttState())).c_str());
      }

      //xx Serial.printf("sent (%d seconds)\n", (millis()-timenow)/1000);

  if(plantowerSampleCount > 0) {
    F64 pm1 = (F64(plantowerPm1Sum) / (F64)plantowerSampleCount);
    F64 pm25 = (F64(plantowerPm25Sum) / (F64)plantowerSampleCount);
    F64 pm10 = (F64(plantowerPm10Sum) / (F64)plantowerSampleCount);
  

     json = String("{") +
      "\"plantowerPM1conc\":"     + String(pm1)  + "," + 
      "\"plantowerPM25conc\":"    + String(pm25) + "," + 
      "\"plantowerPM10conc\":"    + String(pm10) + "," +
      "\"plantowerSampleCount\":" + String(plantowerSampleCount) + "}";


    //xx Serial.println(json);

    timenow = millis();
    mqttPublishTelemetry(json);
    reportPlantowerSensorStatus();
  }
  activateLed(NONE);


  if(BME_ok) {

    F32 pres, temp, hum;

    BME280I2C::Settings settings(
           BME280::OSR_X16,    // Temp   --> default was OSR_1X
           BME280::OSR_X16,    // Humid  --> default was OSR_1X
           BME280::OSR_X1,    // Press
           BME280::Mode_Forced,   // Power mode
           BME280::StandbyTime_1000ms, 
           BME280::Filter_Off,    // Pressure filter
           BME280::SpiEnable_False,
           0x76 // I2C address. I2C specific.
        );

    // Temperature sensor
    bme.setSettings(settings);


    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);

    //xx Serial.print("Temp: ");
    //xx Serial.print(temp);
    //xx Serial.print("°"+ String(TEMPERATURE_UNIT == BME280::TempUnit_Celsius ? 'C' :'F'));
    //xx Serial.print("\t\tHumidity: ");
    //xx Serial.print(hum);
    //xx Serial.print("% RH");
    //xx Serial.print("\t\tPressure: ");
    //xx Serial.print(pres);
    //xx Serial.println(" Pa");


    // TODO: Convert to arduinoJson
    json = "{\"temperature\":" + String(temp) + ",\"humidity\":" + String(hum) + ",\"pressure\":" + String(pres) + "}";

    //xx Serial.println(json);
    timenow = millis();

    if(!mqttPublishTelemetry(json)) {
      //xx Serial.printf("Could not publish environmental data: %s\n", json.c_str());
    }


    BME280I2C::Settings settings2(
           BME280::OSR_X1,    // Temp   --> default was OSR_1X
           BME280::OSR_X1,    // Humid  --> default was OSR_1X
           BME280::OSR_X1,    // Press
           BME280::Mode_Forced,   // Power mode
           BME280::StandbyTime_1000ms, 
           BME280::Filter_Off,    // Pressure filter
           BME280::SpiEnable_False,
           0x76 // I2C address. I2C specific.
        );

    // Temperature sensor
    bme.setSettings(settings2);


    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);

    TemperatureSmoothingFilter.Filter(temp);
   

    // TODO: Convert to arduinoJson
    json = "{\"temperature_smoothed\":" + String(TemperatureSmoothingFilter.Current()) + "}";
    if(!mqttPublishTelemetry(json)) {
      //xx Serial.printf("Could not publish cumulative environmental data: %s\n", json.c_str());
    }
    //xx Serial.printf("sent (%d seconds)\n", (millis()-timenow)/1000);

  }
}



void copy(char *dest, const char *source, U32 destSize) {
  strncpy(dest, source, destSize);
  dest[destSize] = '\0';
}



void processConfigCommand(const String &command) {
  if(command == "uptime") {
    //xx Serial.print("Uptime: ");
    if(millisOveflows > 0) {
      //xx Serial.printf("%d*2^32 + ", millisOveflows);
    }
    //xx Serial.printf("%d seconds\n", millis() / SECONDS);
  }
  else if(command.startsWith("set wifi pw")) {
    updateWifiPassword(&command.c_str()[12]);

    //xx Serial.printf("Saved wifi pw: %s\n", wifiPassword, 123);
  }
  else if(command.startsWith("set local pw")) {
    if(strlen(&command.c_str()[13]) < 8 || strlen(&command.c_str()[13]) > sizeof(localPassword) - 1) {
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
    connectToWiFi(wifiSsid, wifiPassword, true);
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

  //xx Serial.println("Pausing data collection while we scan available networks...");
  WiFi.scanNetworks(false, true);    // Include hidden access points


  U32 scanStartTime = millis();

  // Wait for scan to complete with max 30 seconds
  const int maxTime = 30;
  while(!WiFi.scanComplete()) { 
    if(millis() - scanStartTime > maxTime * SECONDS) {
      //xx Serial.printf("Scan timed out (max %d seconds)\n", maxTime);
      lastScanTime = millis();
      return;
    }
  }

  printScanResult(millis() - scanStartTime);

  lastScanTime = millis();
  resetDataCollection();
}


void updateLocalSsid(const char *ssid) {
  copy(localSsid, ssid, sizeof(localSsid) - 1);
  writeStringToEeprom(LOCAL_SSID_ADDRESS, sizeof(localSsid) - 1, localSsid);
  publishLocalCredentials();
}


void updateLocalPassword(const char *password) {
  copy(localPassword, password, sizeof(localPassword) - 1);
  writeStringToEeprom(LOCAL_PASSWORD_ADDRESS, sizeof(localPassword) - 1, localPassword);
  pubSubConnectFailures = 0;

  publishLocalCredentials();
}


void updateWifiSsid(const char *ssid) {
  copy(wifiSsid, ssid, sizeof(wifiSsid) - 1);
  writeStringToEeprom(WIFI_SSID_ADDRESS, sizeof(wifiSsid) - 1, wifiSsid);
  changedWifiCredentials = true;
  // initiateConnectionToWifi();

  //xx Serial.printf("Saved wifi ssid: %s\n", wifiSsid);
}


void updateWifiPassword(const char *password) {
  copy(wifiPassword, password, sizeof(wifiPassword) - 1);
  writeStringToEeprom(WIFI_PASSWORD_ADDRESS, sizeof(wifiPassword) - 1, wifiPassword);
  changedWifiCredentials = true;
  pubSubConnectFailures = 0;
  // initiateConnectionToWifi();

  //xx Serial.printf("Saved wifi password: %s\n", wifiPassword);
}


void updateMqttUrl(const char *url) {
  copy(mqttUrl, url, sizeof(mqttUrl) - 1);
  writeStringToEeprom(MQTT_URL_ADDRESS, sizeof(mqttUrl) - 1, mqttUrl);
  pubSubConnectFailures = 0;
  mqttDisconnect();
  mqttServerConfigured = false;
  mqttServerLookupError = false;

  setupPubSubClient();

  // Let's immediately connect our PubSub client
  reconnectToPubSubServer();
}


void updateDeviceToken(const char *token) {
  copy(deviceToken, token, sizeof(deviceToken) - 1);
  writeStringToEeprom(DEVICE_KEY_ADDRESS, sizeof(deviceToken) - 1, deviceToken);
  pubSubConnectFailures = 0;
}


int updateMqttPort(const char *port) {
  mqttPort = atoi(port);
  if(port == 0)
    return 0;

  EepromWriteU16(PUB_SUB_PORT_ADDRESS, mqttPort);
  pubSubConnectFailures = 0;
  mqttDisconnect();
  mqttServerConfigured = false;
  mqttServerLookupError = false;

  setupPubSubClient();

  // Let's immediately connect our PubSub client
  reconnectToPubSubServer();

  return 1;
}


void updateSampleDuration(const char *duration) {
  setSampleDuration(atoi(duration));
  EepromWriteU16(SAMPLE_DURATION_ADDRESS, sampleDuration);

  publishSampleDuration();
}

void setSampleDuration(U16 duration) {
  sampleDuration = duration;
  sampleDuration_micros = U32(duration) * SECONDS * MILLIS_TO_MICROS;
}


void setWifiSsidFromScanResults(int index) {
  if(WiFi.scanComplete() == -1) {
    //xx Serial.println("Scan running... please wait for it to complete and try again.");
    return;
  }

  if(WiFi.scanComplete() == -2) {
    //xx Serial.println("You must run \"scan\" first!");
    return;
  }

  // Networks are 0-indexed, but user will be selecting a network based on 1-indexed display
  if(index < 1 || index > WiFi.scanComplete()) {
    //xx Serial.printf("Invalid index: %s\n", index);
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


// SerialEvent occurs whenever a new data comes in the hardware serial RX. This
// routine is run between each time loop() runs, so using delay inside loop can
// delay response. Multiple bytes of data may be available.
void checkForNewInputFromSerialPort() {
  // while (Serial.available()) {
  //   // get the new byte:
  //   char incomingChar = (char)Serial.read();
  //   // Add it to the command.
  //   // if the incoming character is a newline, or we're just getting too long (which should never happen) 
  //   // start processing the command
  //   if (incomingChar == '\n' || command.length() == MAX_COMMAND_LENGTH) {
  //     processConfigCommand(command);
  //     command = "";
  //   }
  //   else
  //     command += incomingChar;
  // }
}


// Get a list of wifi hotspots the device can see
void printScanResult(U32 duration) {
  int networksFound = WiFi.scanComplete();

  publishStatusMessage("scan results: " + String(networksFound) + " hotspots found in " + String(duration) + "ms");

  if(networksFound < 0) {
    //xx Serial.println("NEGATIVE RESULT!!");    // Should never happen
    return;
  }
  if (networksFound == 0) {
    //xx Serial.printf("Scan completed (%d seconds): No networks found\n", duration / SECONDS);
    return;
  }

  //xx Serial.printf("%d network(s) found (%d seconds)\n", networksFound, duration / SECONDS);

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

  if(!mqttPublishAttribute(json)) {
      publishStatusMessage("Could not upload scan results");

    //xx Serial.printf("Could not publish message: %s\n", json.c_str());
  }
}


void ping(const char *target) {
  connectToWiFi(wifiSsid, wifiPassword, false);

  //xx Serial.print("Pinging ");   Serial.println(target);
  U8 pingCount = 5;
  while(pingCount > 0)
  {
    if(Ping.ping(target, 1)) {
      //xx Serial.printf("Response time: %d ms\n", Ping.averageTime());
      pingCount--;
      if(pingCount == 0) {
        //xx Serial.println("Ping complete");
      }
    } else {
      //xx Serial.printf("Failure pinging  %s\n", target);
      pingCount = 0;    // Cancel ping if it's not working
    }
  }
}


// void contactServer(const String &url) {

//   IPAddress google;
  
//   if(!WiFi.hostByName(url.c_str(), google)) {
//     //xx Serial.println("Could not resolve hostname -- retrying in 5 seconds");
//     delay(5000);
//     return;
//   }

//   //  wfclient2.setTimeout(20 * 1000);
  
//   WiFiClient wfclient2;
//   if(wfclient2.connect(google, 80)) {
//     //xx Serial.print("connected to ");
//     //xx Serial.println(url);
//     wfclient2.println("GET / HTTP/1.0");
//     wfclient2.println("");
//     needToConnect = false;
//   }
//   else {
//     //xx Serial.println("connection failed");
//   }
  
// //print results from google

//   unsigned long timeout = millis();
//   while (wfclient2.available() == 0) {
//     if (millis() - timeout > 5000) {
//       //xx Serial.println(">>> Client Timeout !");
//       wfclient2.stop();
//       return;
//     }
//   }

//   while(wfclient2.available()) {
//     String line = wfclient2.readStringUntil('\\n');
//     //xx Serial.println(line);
//   }
//   for(int i = 0; i < 5; i++) {
//     //xx Serial.println("x");
//   }
// }


// Called from setup
void setupLocalAccessPoint(const char *ssid, const char *password)
{
  // Resolve addresses
  IPAddress ip, gateway;
  WiFi.hostByName(localAccessPointAddress, ip);
  WiFi.hostByName(localGatewayAddress, gateway);

  IPAddress subnetMask(255,255,255,0);
 
  bool ok = WiFi.softAPConfig(ip, gateway, subnetMask) && WiFi.softAP(ssid, password); //, wifiChannel, false);  // name, pw, channel, hidden


  // Serial.printf("AP SSID: %s\n", Wifi.soft)
  //xx Serial.printf("AP IP Address: %s\n", WiFi.softAPIP().toString().c_str());

  const char *dnsName = "birdhouse";      // Connect with birdhouse.local

  if (MDNS.begin(dnsName)) {              // Start the mDNS responder for birdhouse.local
    //xx Serial.printf("mDNS responder started: %s.local\n", dnsName);
  }
  else {
    //xx Serial.println("Error setting up MDNS responder!");
  }
}


void connectToWiFi(const char *ssid, const char *password, bool disconnectIfConnected = false) {

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
  // set passphrase
  int status = WiFi.begin(wifiSsid, wifiPassword);

  if (status == WL_CONNECT_FAILED) {    // Something went wrong
    //xx Serial.println("Failed to set ssid & pw.  Connect attempt aborted.");
  } else {                              // Status is probably WL_DISCONNECTED
//xx Serial.println("Setting isConnecting to true!");
    isConnectingToWifi = true;
    connectingToWifiDotTimer = millis();
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

  if(millis() - connectingToWifiDotTimer > 500) {
    //xx Serial.print(".");
    connectingToWifiDotTimer = millis();
  }

  if(millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
    //xx Serial.println("");
    //xx Serial.println("Unable to connect to WiFi!");
    isConnectingToWifi = false;
  }
}


// We just connected (or reconnected) to wifi
void onConnectedToWifi() {
  server.begin();

  // Switch over to Plantower
  Serial.println("Turning over the serial port to the Plantower... no more messages here.");
  Serial.flush();   // Get any last bits out of there before switching the serial pins


  if(!serialSwapped) {  
    Serial.begin(PLANTOWER_SERIAL_BAUD_RATE);
    Serial1.begin(115200);
    Serial.swap();    // D8 is now TX, D7 RX
    serialSwapped = true;
  }
}


void writeStringToEeprom(int addr, int length, const char *value)
{
  for (int i = 0; i < length; i++)
    EEPROM.write(addr + i, value[i]);
        
  EEPROM.write(addr + length, '\0');
  EEPROM.commit();
}


void readStringFromEeprom(int addr, int length, char container[])
{
  for (int i = 0; i < length; i++)
    container[i] = EEPROM.read(addr + i);

  container[length] = '\0';   // Better safe than sorry!
}


// This function will write a 2 byte integer to the eeprom at the specified address and address + 1
void EepromWriteU16(int addr, U16 value)
{
  byte lowByte  = ((value >> 0) & 0xFF);
  byte highByte = ((value >> 8) & 0xFF);

  EEPROM.write(addr, lowByte);
  EEPROM.write(addr + 1, highByte);
  EEPROM.commit();
}


// This function will read a 2 byte integer from the eeprom at the specified address and address + 1
U16 EepromReadU16(int addr)
{
  byte lowByte  = EEPROM.read(addr);
  byte highByte = EEPROM.read(addr + 1);

  return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
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

  //xx Serial.printf("[status] %s\n", msg.c_str());
  mqttPublishAttribute(json);  
}


void setupOta() {

  // Are these ever called?
  ArduinoOTA.onStart([]() {
    activateLed(RED | YELLOW);
    //xx Serial.println("OTA updates started");
    publishOtaStatusMessage("Starting update");
  });


  ArduinoOTA.onEnd([]() {
    //xx Serial.println("OTA updates finished");
    publishOtaStatusMessage("Update successful");
    for(int i = 0; i < 10; i++) { activateLed(GREEN); delay(50); activateLed(NONE); delay(50); }
    activateLed(RED | YELLOW | GREEN);
  });


  static bool lastProgressUpdate = false;

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //xx Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    activateLed(YELLOW | lastProgressUpdate ? GREEN : RED);
    lastProgressUpdate = !lastProgressUpdate;
  });


  ArduinoOTA.onError([](ota_error_t error) {
    //xx Serial.printf("Error[%u]: ", error);

    String msg;
    if (error == OTA_AUTH_ERROR)         msg = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR)   msg = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) msg = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) msg = "Receive Failed";
    else if (error == OTA_END_ERROR)     msg = "End Failed";

    //xx Serial.println(msg);
    publishOtaStatusMessage("Update failed: " + msg);

    ESP.restart();
  });


  ArduinoOTA.begin();
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

