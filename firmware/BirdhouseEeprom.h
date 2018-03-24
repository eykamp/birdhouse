#ifndef EepromUtils_h
#define EepromUtils_h

#define NO_GLOBAL_EEPROM        // Suppress creation of EEPROM object... we'll create our own below

#include <EEPROM.h>             // Parent

#include "Types.h"


// This class helps manage the variables we persist in EEPROM

class BirdhouseEeprom : public EEPROMClass {

private:

  typedef EEPROMClass Parent;

  // Sizes
  static const int SSID_LENGTH            = 32;
  static const int PASSWORD_LENGTH        = 63;
  static const int DEVICE_KEY_LENGTH      = 20;
  static const int URL_LENGTH             = 64;
  static const int SENTINEL_MARKER_LENGTH = 64;
  static const int SERIAL_NUMBER_LENGTH   = 32;   // For now this is just 3 chars, but could eventually be a GUID.  Let's reserve the space now.

  // The vars we store in EEPROM
  char localSsid[SSID_LENGTH + 1];
  char localPassword[PASSWORD_LENGTH + 1];
  char wifiSsid[SSID_LENGTH + 1];
  char wifiPassword[PASSWORD_LENGTH + 1];
  char deviceToken[DEVICE_KEY_LENGTH + 1];
  char mqttUrl[URL_LENGTH + 1];
  char serialNumber[SERIAL_NUMBER_LENGTH + 1];
  
  U16 mqttPort;
  U16 sampleDuration;     // In seconds

  const char SENTINEL_MARKER[SENTINEL_MARKER_LENGTH + 1] = "SensorBot by Chris Eykamp -- v106";   // Changing this will cause devices to revert to default configuration



  // Addresses
  static const int LOCAL_SSID_ADDRESS      = 0;
  static const int LOCAL_PASSWORD_ADDRESS  = LOCAL_SSID_ADDRESS      + sizeof(localSsid);
  static const int WIFI_SSID_ADDRESS       = LOCAL_PASSWORD_ADDRESS  + sizeof(localPassword);
  static const int WIFI_PASSWORD_ADDRESS   = WIFI_SSID_ADDRESS       + sizeof(wifiSsid);
  static const int DEVICE_KEY_ADDRESS      = WIFI_PASSWORD_ADDRESS   + sizeof(wifiPassword);
  static const int MQTT_URL_ADDRESS        = DEVICE_KEY_ADDRESS      + sizeof(deviceToken);
  static const int PUB_SUB_PORT_ADDRESS    = MQTT_URL_ADDRESS        + sizeof(mqttUrl);
  static const int SAMPLE_DURATION_ADDRESS = PUB_SUB_PORT_ADDRESS    + sizeof(mqttPort);
  static const int SERIAL_NUMBER_ADDRESS   = SAMPLE_DURATION_ADDRESS + sizeof(sampleDuration);
  static const int SENTINEL_ADDRESS        = SERIAL_NUMBER_ADDRESS   + sizeof(serialNumber);
  static const int NEXT_ADDRESS            = SENTINEL_ADDRESS        + sizeof(SENTINEL_MARKER); 
  static const int EEPROM_SIZE = NEXT_ADDRESS;



public:

  void begin() {
    Parent::begin(EEPROM_SIZE);

    readStringFromEeprom(LOCAL_SSID_ADDRESS,     sizeof(localSsid)     - 1, localSsid);
    readStringFromEeprom(LOCAL_PASSWORD_ADDRESS, sizeof(localPassword) - 1, localPassword);
    readStringFromEeprom(WIFI_SSID_ADDRESS,      sizeof(wifiSsid)      - 1, wifiSsid);
    readStringFromEeprom(WIFI_PASSWORD_ADDRESS,  sizeof(wifiPassword)  - 1, wifiPassword);
    readStringFromEeprom(DEVICE_KEY_ADDRESS,     sizeof(deviceToken)   - 1, deviceToken);
    readStringFromEeprom(MQTT_URL_ADDRESS,       sizeof(mqttUrl)       - 1, mqttUrl);
    readStringFromEeprom(SERIAL_NUMBER_ADDRESS,  sizeof(serialNumber)  - 1, serialNumber);

    mqttPort       = EepromReadU16(PUB_SUB_PORT_ADDRESS);
    sampleDuration = EepromReadU16(SAMPLE_DURATION_ADDRESS);
  }


  void setLocalSsid(const char *ssid) {
    copy(localSsid, ssid, sizeof(localSsid) - 1);
    writeStringToEeprom(LOCAL_SSID_ADDRESS, sizeof(localSsid) - 1, localSsid);
  }

  const char *getLocalSsid() {
    return localSsid;
  }


  void setLocalPassword(const char *password) {
    copy(localPassword, password, sizeof(localPassword) - 1);
    writeStringToEeprom(LOCAL_PASSWORD_ADDRESS, sizeof(localPassword) - 1, localPassword);
  }

  const char *getLocalPassword() {
    return localPassword;
  }


  int getLocalPasswordSize() {
    return sizeof(localPassword);
  }


  void setWifiSsid(const char *ssid) {
    copy(wifiSsid, ssid, sizeof(wifiSsid) - 1);
    writeStringToEeprom(WIFI_SSID_ADDRESS, sizeof(wifiSsid) - 1, wifiSsid);
  }

  const char *getWifiSsid() {
    return wifiSsid;
  }


  void setWifiPassword(const char *password) {
    copy(wifiPassword, password, sizeof(wifiPassword) - 1);
    writeStringToEeprom(WIFI_PASSWORD_ADDRESS, sizeof(wifiPassword) - 1, wifiPassword);
  }

  const char *getWifiPassword() {
    return wifiPassword;
  }


  void setMqttUrl(const char *url) {
    copy(mqttUrl, url, sizeof(mqttUrl) - 1);
    writeStringToEeprom(MQTT_URL_ADDRESS, sizeof(mqttUrl) - 1, mqttUrl);
  }  


  const char *getMqttUrl() {
    return mqttUrl;
  }


  void setSerialNumber(const char *serialNum) {
    copy(serialNumber, serialNum, sizeof(serialNumber) - 1);
    writeStringToEeprom(SERIAL_NUMBER_ADDRESS, sizeof(serialNumber) - 1, serialNumber);
  }


  const char *getSerialNumber() {
    return serialNumber;
  }


  void setDeviceToken(const char *token) {
    copy(deviceToken, token, sizeof(deviceToken) - 1);
    writeStringToEeprom(DEVICE_KEY_ADDRESS, sizeof(deviceToken) - 1, deviceToken);
  }

  const char *getDeviceToken() {
    return deviceToken;
  }


  void setMqttPort(const char *port) {
    mqttPort = atoi(port);
    EepromWriteU16(PUB_SUB_PORT_ADDRESS, mqttPort);
  }

  U16 getMqttPort() {
    return mqttPort;
  }


  void setSampleDuration(const char *duration) {
    sampleDuration = atoi(duration);
    EepromWriteU16(SAMPLE_DURATION_ADDRESS, sampleDuration);
  }

  U16 getSampleDuration() {
    return sampleDuration;
  }

  void writeSentinelMarker() {
    writeStringToEeprom(SENTINEL_ADDRESS, sizeof(SENTINEL_MARKER) - 1, SENTINEL_MARKER);
  }


  bool verifySentinelMarker() {
    char storedSentinelMarker[SENTINEL_MARKER_LENGTH + 1];
    readStringFromEeprom(SENTINEL_ADDRESS, sizeof(storedSentinelMarker) - 1, storedSentinelMarker);

    // Return true if the marker is there, false otherwise
    return (strcmp(SENTINEL_MARKER, storedSentinelMarker) == 0);
  }


private:
  // Utility functions

  void writeStringToEeprom(int addr, int length, const char *value)
  {
    for (int i = 0; i < length; i++)
      write(addr + i, value[i]);
          
    write(addr + length, '\0');
    commit();
  }


  void readStringFromEeprom(int addr, int length, char container[])
  {
    for (int i = 0; i < length; i++)
      container[i] = read(addr + i);

    container[length] = '\0';   // Better safe than sorry!
  }


  // This function will write a 2 byte integer to the eeprom at the specified address and address + 1
  void EepromWriteU16(int addr, U16 value)
  {
    byte lowByte  = ((value >> 0) & 0xFF);
    byte highByte = ((value >> 8) & 0xFF);

    write(addr, lowByte);
    write(addr + 1, highByte);
    commit();
  }


  // This function will read a 2 byte integer from the eeprom at the specified address and address + 1
  U16 EepromReadU16(int addr)
  {
    byte lowByte  = read(addr);
    byte highByte = read(addr + 1);

    return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
  }


  void copy(char *dest, const char *source, U32 destSize) {
    strncpy(dest, source, destSize);
    dest[destSize] = '\0';
  }



};


BirdhouseEeprom Eeprom;   // Create an instance of our BiridhouseEeprom

#endif