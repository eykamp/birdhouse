#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <PubSubClient.h>       // For MQTT
#include <Dns.h>
#include <EEPROM.h>             // For persisiting values in EEPROM
#include "ESP8266Ping.h"        // For ping, of course

// Indulge me!
#define U8  uint8_t
#define S8  int8_t
#define S16 int16_t
#define U16 uint16_t
#define S32 int32_t 
#define U32 uint32_t            // unsigned long

#define SECONDS 1000

//////////////////////
// WiFi Definitions //
//////////////////////

//WiFiServer server(80);
ESP8266WebServer server(80);
void handleRoot();              // function prototypes for HTTP handlers
void handleLogin();
void handleNotFound();
 
///// 
// For persisting values in EEPROM
const int SSID_LENGTH       = 32;
const int PASSWORD_LENGTH   = 63;
const int DEVICE_KEY_LENGTH = 20;
const int URL_LENGTH        = 64;


const int LOCAL_SSID_ADDRESS     = 0;
const int LOCAL_PASSWORD_ADDRESS = LOCAL_SSID_ADDRESS     + SSID_LENGTH       + 1;
const int WIFI_SSID_ADDRESS      = LOCAL_PASSWORD_ADDRESS + PASSWORD_LENGTH   + 1;
const int WIFI_PASSWORD_ADDRESS  = WIFI_SSID_ADDRESS      + SSID_LENGTH       + 1;
const int DEVICE_KEY_ADDRESS     = WIFI_PASSWORD_ADDRESS  + PASSWORD_LENGTH   + 1;
const int MOTHERSHIP_URL_ADDRESS = DEVICE_KEY_ADDRESS     + DEVICE_KEY_LENGTH + 1;

const int EEPROM_SIZE = 2 * (SSID_LENGTH + 1) + 2 * (PASSWORD_LENGTH + 1) + (DEVICE_KEY_LENGTH + 1) + (URL_LENGTH + 1);


// Our vars to hold the EEPROM values
char localSsid[SSID_LENGTH + 1];
char localPassword[PASSWORD_LENGTH + 1];
char wifiSsid[SSID_LENGTH + 1];
char wifiPassword[PASSWORD_LENGTH + 1];
char deviceKey[DEVICE_KEY_LENGTH + 1];
char mothershipUrl[URL_LENGTH + 1];

/////

const int PUB_SUB_CLIENT_PORT = 80;

// U8 macAddress[WL_MAC_ADDR_LENGTH];
// WiFi.softAPmacAddress(macAddress);

U32 millisOveflows = 0;

const U32 WIFI_CONNECT_TIMEOUT = 20 * SECONDS;


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

WiFiClient wfclient;
PubSubClient pubSubClient(wfclient);

void reconnect() {
  // Loop until we're reconnected
  while (!pubSubClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (pubSubClient.connect("arduinoClient", "PjoBPqgAB9Dkso8JUo6N", "")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      pubSubClient.publish("v1/devices/me/telemetry","{'humidity':-999}");
      // ... and resubscribe
      pubSubClient.subscribe("v1/devices/me/attributes");
    } else {
      Serial.print("failed, rc=");
      Serial.print(pubSubClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



const char *defaultPingTargetHostName = "www.google.com";
// const int STARTUP = 0;
// const int RUNNING = 1;



void connectToWiFi(const char*, const char*, bool); // Forward declare

// U8 mode = STARTUP;

const U32 MAX_COMMAND_LENGTH = 128;
String command;     // The command the user is composing during command mode

bool changedWifiCredentials = false;    // Track if we've changed wifi connection params during command mode

void setup()
{
  Serial.begin(115200);
  while(!Serial) { }    // wait for serial port to connect  ==> needed?

  Serial.println("");
  Serial.println("");
  
  
  Serial.println("   _____                            __          __ ");
  Serial.println("  / ___/___  ____  _________  _____/ /_  ____  / /_");
  Serial.println("  \\__ \\/ _ \\/ __ \\/ ___/ __ \\/ ___/ __ \\/ __ \\/ __/");
  Serial.println(" ___/ /  __/ / / (__  ) /_/ / /  / /_/ / /_/ / /_  ");
  Serial.println("/____/\\___/_/ /_/____/\\____/_/  /_.___/\\____/\\__/  ");
                                                   
  Serial.println("");


  EEPROM.begin(EEPROM_SIZE);

  // void readStringFromEeprom(int addr, int length, char container[]);  // Forward declare

  readStringFromEeprom(LOCAL_SSID_ADDRESS,     SSID_LENGTH,       localSsid);
  readStringFromEeprom(LOCAL_PASSWORD_ADDRESS, PASSWORD_LENGTH,   localPassword);
  readStringFromEeprom(WIFI_SSID_ADDRESS,      SSID_LENGTH,       wifiSsid);
  readStringFromEeprom(WIFI_PASSWORD_ADDRESS,  PASSWORD_LENGTH,   wifiPassword);
  readStringFromEeprom(DEVICE_KEY_ADDRESS,     DEVICE_KEY_LENGTH, deviceKey);
  readStringFromEeprom(MOTHERSHIP_URL_ADDRESS, URL_LENGTH,        mothershipUrl);
  
  Serial.print("localSsid: ");     Serial.println(localSsid);
  Serial.print("localPassword: "); Serial.println(localPassword);
  Serial.print("wifiSsid: ");      Serial.println(wifiSsid);
  Serial.print("wifiPassword: ");  Serial.println(wifiPassword);
  Serial.print("mothershipUrl: "); Serial.println(mothershipUrl);
  Serial.print("deviceKey: ");     Serial.println(deviceKey);

  WiFi.mode(WIFI_AP);  
  



  setupPubSubClient();
  pubSubClient.setCallback(callback);

 
  server.on("/", HTTP_GET, handleRoot);        // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/login", HTTP_POST, handleLogin); // Call the 'handleLogin' function when a POST request is made to URI "/login"
  server.onNotFound(handleNotFound);           // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

  server.begin(); 
  
  command.reserve(MAX_COMMAND_LENGTH);

  setupLocalAccessPoint(localSsid, localPassword);
  connectToWiFi(wifiSsid, wifiPassword, changedWifiCredentials);
}


void setupPubSubClient() {
  IPAddress serverIp;

  if(!WiFi.hostByName(mothershipUrl, serverIp)) {
    Serial.print("Could not get IP address for server ");   Serial.println(mothershipUrl);
  }
  else
    pubSubClient.setServer(serverIp, PUB_SUB_CLIENT_PORT);
  }


void activateLed(String LED) {

  Serial.print("Your post was received: ");
  Serial.println(translate(LED));
  //activate Led pins 
}

bool needToConnect = false;
String visitUrl = "www.yahoo.com";




bool isConnectingToWifi = false;
U32 connectingToWifiDotTimer;
U32 wifiConnectStartTime;
U32 lastMillis = 0;

void loop() {
  U32 time = millis();

  if(time < lastMillis)
    millisOveflows++;
  
  lastMillis = time;


  server.handleClient();
  serialEvent();

  if(isConnectingToWifi)
    connectingToWifi();
  
  if(needToConnect) {
    contactServer(visitUrl);
  }
  
////handle pubSubClient

//  if (!pubSubClient.connected()) {
//    reconnect();
//  }
//  pubSubClient.loop();
}


void copy(char *dest, const char *source, U32 size) {
  strncpy(dest, source, size);
  dest[size] = '\0';
}



void processConfigCommand(const String &command) {
  if(command == "uptime") {
    Serial.print("Uptime: ");
    if(millisOveflows > 0) {
      Serial.print(millisOveflows);   Serial.print("*2^32 + ");
    }
    Serial.println(millis() / 1000);
  }
    else if(command.startsWith("set wifi pw")) {
    copy(wifiPassword, &command.c_str()[12], PASSWORD_LENGTH);
    writeStringToEeprom(WIFI_PASSWORD_ADDRESS, PASSWORD_LENGTH, wifiPassword);
    changedWifiCredentials = true;

    Serial.print("Set wifi pw: ");   Serial.println(wifiPassword);
  }
  else if(command.startsWith("set local pw")) {
    copy(localPassword, &command.c_str()[13], PASSWORD_LENGTH);
    writeStringToEeprom(LOCAL_PASSWORD_ADDRESS, PASSWORD_LENGTH, localPassword);

    Serial.print("Set local pw: ");  Serial.println(localPassword);
  }
  else if(command.startsWith("set wifi ssid")) {
    copy(wifiSsid, &command.c_str()[14], SSID_LENGTH);
    writeStringToEeprom(WIFI_SSID_ADDRESS, SSID_LENGTH, wifiSsid);
    changedWifiCredentials = true;

    Serial.print("Set wifi ssid: ");   Serial.println(wifiSsid);
  }
  else if(command.startsWith("set local ssid")) {
    copy(localSsid, &command.c_str()[15], SSID_LENGTH);
    writeStringToEeprom(LOCAL_SSID_ADDRESS, SSID_LENGTH, localSsid);

    Serial.print("Set local ssid: ");  Serial.println(localSsid);
  }
  else if(command.startsWith("set device key")) {
    copy(deviceKey, &command.c_str()[15], DEVICE_KEY_LENGTH);
    writeStringToEeprom(DEVICE_KEY_ADDRESS, DEVICE_KEY_LENGTH, deviceKey);

    Serial.print("Set device key: ");  Serial.println(deviceKey);
  }  
  else if(command.startsWith("set mothership url")) {
    copy(mothershipUrl, &command.c_str()[18], URL_LENGTH);
    writeStringToEeprom(DEVICE_KEY_ADDRESS, URL_LENGTH, mothershipUrl);

    Serial.print("Set mothership URL: ");   Serial.println(mothershipUrl);
    setupPubSubClient();
  }
  else if(command.startsWith("connect")) {
    connectToWiFi(wifiSsid, wifiPassword, true);
  }
  else if(command.startsWith("cancel")) {
    if(isConnectingToWifi) {
      Serial.println("\nCanceled connection attempt");
      isConnectingToWifi = false;
    }
    else
      Serial.println("No connection attempt in progress");

  }
  else if(command.startsWith("show")) {
    Serial.print("localSsid: ");     Serial.println(localSsid);
    Serial.print("localPassword: "); Serial.println(localPassword);
    Serial.print("wifiSsid: ");      Serial.println(wifiSsid);
    Serial.print("wifiPassword: ");  Serial.println(wifiPassword);
    Serial.print("mothershipUrl: "); Serial.println(mothershipUrl);
    Serial.print("deviceKey: ");     Serial.println(deviceKey);
  }
  else if(command.startsWith("diag")) {
    Serial.println("Wifi Diagnostics:");
    WiFi.printDiag(Serial); 
    Serial.print("Wifi connection status: ");   Serial.println(getWifiStatusName(WiFi.status()));
  }
  else if(command.startsWith("scan")) {
    scanAccessPoints();
  }
  else if(command.startsWith("ping")) {
    const char *target = (command.length() > 5) ? &command.c_str()[5] : defaultPingTargetHostName;
    
    connectToWiFi(wifiSsid, wifiPassword, false);
    Serial.print("Pinging ");   Serial.println(target);
    U8 pingCount = 5;
    while(pingCount > 0)
    {
      if(Ping.ping(target, 1)) {
        Serial.print("Response time:"); Serial.print(Ping.averageTime()); Serial.println("ms");
        pingCount--;
        if(pingCount == 0)
          Serial.println("Ping complete");
      } else {
        Serial.print("Failure pinging ");   Serial.println(target);
        pingCount = 0;    // Cancel ping if it's not working
      }
    }
  }
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


// SerialEvent occurs whenever a new data comes in the hardware serial RX. This
// routine is run between each time loop() runs, so using delay inside loop can
// delay response. Multiple bytes of data may be available.
void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char incomingChar = (char)Serial.read();
    // Add it to the command.
    // if the incoming character is a newline, or we're just getting too long (which should never happen) 
    // start processing the command
    if (incomingChar == '\n' || command.length() == MAX_COMMAND_LENGTH) {
      processConfigCommand(command);
      command = "";
    }
    else
      command += incomingChar;
  }
}


void scanAccessPoints() {
  Serial.println("Scanning available networks...");

  int networksFound = WiFi.scanNetworks();
  Serial.print("Scan done; ");
  if (networksFound == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(networksFound);
    Serial.println(" networks found:");
    for (int i = 0; i < networksFound; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
    }
    Serial.println("[* = secured network]");
  }
  Serial.println("");
}


void contactServer(const String &url) {

  IPAddress google;
  
  if(!WiFi.hostByName(url.c_str(), google)) {
    Serial.println("Could not resolve hostname -- retrying in 5 seconds");
    delay(5000);
    return;
  }

  //  wfclient2.setTimeout(20 * 1000);
  
  WiFiClient wfclient2;
  if(wfclient2.connect(google, 80)) {
    Serial.print("connected to ");
    Serial.println(url);
    wfclient2.println("GET / HTTP/1.0");
    wfclient2.println("");
    needToConnect = false;
  }
  else {
    Serial.println("connection failed");
  }
  
//print results from google

  unsigned long timeout = millis();
  while (wfclient2.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      wfclient2.stop();
      return;
    }
  }

  while(wfclient2.available()){
    String line = wfclient2.readStringUntil('\\n');
    Serial.println(line);
  }
  for(int i = 0; i < 5; i++) {
    Serial.println("x");
  }


}

void handleRoot() {
//  server.send(200, "text/plain", "Hello world!");

//  server.send(200, "text/html", "<form action=\\"/login\\" method=\\"POST\\"><input type=\\"text\\" name=\\"LED\\" placeholder=\\"Username\\"><input type='submit' value='Submit'></form>");
  server.send(200, "text/html", "<form action='/login' method='POST'>"
  "<input type='radio' name='LED' value = 'red'>"
  "<input type='radio' name='LED' value = 'yellow'>"
  "<input type='radio' name='LED' value = 'green'>"
  "<input type='text' name='url' value = '' size='30'>"
  "<input type='submit' value='Submit'></form>"); 
//  client.print(getHeader());
//  client.print("howdy");
//  client.print(getFooter());
//  delay(1);
//  Serial.println("Client disonnected");
//
//  // The client will actually be disconnected
//  // when the function returns and 'client' object is detroyed
}

void handleLogin() {
  if( ! server.hasArg("LED")
      || server.arg("LED") == NULL) {
    server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }

  visitUrl = server.arg("url");
  needToConnect = true;
  activateLed(server.arg("LED"));
  server.sendHeader("Location","/");        // Add a header to respond with a new location for the browser to go to the home page again
  server.send(303);                         // Send it back to the browser with an HTTP status 303 (See Other) to redirect
  
}

void handleNotFound(){
  server.send(200, "text/plain", "Not found!! Try /");
 
//  client.print(getHeader());
//  client.print("see ya");
//  client.print(getFooter());
//  delay(1);
//  Serial.println("Client disonnected");
}

  // Read the first line of the request
//  String req = client.readStringUntil('\\r');
//  Serial.println(req);
//  client.flush();



//const char* getHeader() {
//  return
//    "HTTP/1.1 200 OK\\r\\n"
//    "Content-Type: text/html\\r\\n\\r\\n"
//    "<!DOCTYPE HTML>\\r\\n<html>\\r\\n";
//}
//
//const char* getFooter() {
//  return 
//    "</html>\\n";       
//}


void setupLocalAccessPoint(const char *ssid, const char *password)
{
  IPAddress Ip(192,168,1,1);
  IPAddress NMask(255,255,255,0);
 
  WiFi.softAPConfig(Ip, Ip, NMask);
  WiFi.softAP(ssid, password);  // name, pw, channel, hidden


  IPAddress myIp = WiFi.softAPIP();
  Serial.print("AP IP Address: ");  Serial.println(myIp);

  if (MDNS.begin("esp8266")) {              // Start the mDNS responder for esp8266.local
    Serial.println("mDNS responder started.");
  }
  else {
    Serial.println("Error setting up MDNS responder!");
  }
}


void connectToWiFi(const char *ssid, const char *password, bool disconnectIfConnected = false) {

  changedWifiCredentials = false;

  if(WiFi.status() == WL_CONNECTED) {   // Already connected
    if(!disconnectIfConnected)          // Don't disconnect, so nothing to do
      return;

    Serial.println("Disconnecting..."); // Otherwise... disconnect!
    WiFi.disconnect();
  }

  wifiConnectStartTime = millis();      // Initiate new connection
  Serial.print("Connecting to ");       
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  isConnectingToWifi = true;
  connectingToWifiDotTimer = millis();
 }


// Will be run every loop cycle if isConnectingToWifi is true
void connectingToWifi()
{
  if(WiFi.status() != WL_CONNECTED) {
    if(millis() - connectingToWifiDotTimer > 500) {
      Serial.print(".");
      connectingToWifiDotTimer = millis();
    }

    if(millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
      Serial.println("");
      Serial.println("Unable to connect to WiFi!");
      isConnectingToWifi = false;
    }

    return;
  }

  // We're connected!

  isConnectingToWifi = false;
  
  Serial.println("");
  Serial.print((millis() - wifiConnectStartTime) / 1000);
  Serial.println(" seconds");
  Serial.print("Connected to WiFi; Address on the LAN: "); 
  Serial.println(WiFi.localIP());         // Send the IP address of the ESP8266 to the computer
}



int greenLedPin = 1;
int yellowLedPin = 2;
int redLedPin = 3;
int translate(const String &color) {

  if(color == "green") {
    return greenLedPin;
  }
  if(color == "yellow") {
    return yellowLedPin;
  }
  if(color == "red") {
    return redLedPin;
  }
  else return 0;
 
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
