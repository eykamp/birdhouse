#ifndef EepromUtils_h
#define EepromUtils_h

#define NO_GLOBAL_EEPROM        // Suppress creation of EEPROM object... we'll create our own below

#include <EEPROM.h>             // Parent

#include "Types.h"


// This class helps manage the variables we persist in EEPROM



class BirdhouseEeprom : public EEPROMClass {

// Sizes
static const int SSID_LENGTH             = 32;
static const int PASSWORD_LENGTH         = 63;
static const int DEVICE_KEY_LENGTH       = 20;
static const int URL_LENGTH              = 64;
static const int SENTINEL_MARKER_LENGTH  = 64;
static const int FIRMWARE_VERSION_LENGTH = 12;


  // XMacro!
#define NUMERIC_FIELD_LIST \
  FIELD(temperatureCalibrationFactor, F32,  atof, TEMPERATURE_CALIBRATION_FACTOR_ADDRESS, getTemperatureCalibrationFactor, setTemperatureCalibrationFactor) \
  FIELD(temperatureCalibrationOffset, F32,  atof, TEMPERATURE_CALIBRATION_OFFSET_ADDRESS, getTemperatureCalibrationOffset, setTemperatureCalibrationOffset) \
  FIELD(humidityCalibrationFactor,    F32,  atof, HUMIDITY_CALIBRATION_FACTOR_ADDRESS,    getHumidityCalibrationFactor,    setHumidityCalibrationFactor)    \
  FIELD(humidityCalibrationOffset,    F32,  atof, HUMIDITY_CALIBRATION_OFFSET_ADDRESS,    getHumidityCalibrationOffset,    setHumidityCalibrationOffset)    \
  FIELD(pressureCalibrationFactor,    F32,  atof, PRESURE_CALIBRATION_FACTOR_ADDRESS,     getPressureCalibrationFactor,    setPressureCalibrationFactor)    \
  FIELD(pressureCalibrationOffset,    F32,  atof, PRESURE_CALIBRATION_OFFSET_ADDRESS,     getPressureCalibrationOffset,    setPressureCalibrationOffset)    \
  FIELD(PM10CalibrationFactor,        F32,  atof, PM10_CALIBRATION_FACTOR_ADDRESS,        getPM10CalibrationFactor,        setPM10CalibrationFactor)        \
  FIELD(PM10CalibrationOffset,        F32,  atof, PM10_CALIBRATION_OFFSET_ADDRESS,        getPM10CalibrationOffset,        setPM10CalibrationOffset)        \
  FIELD(PM25CalibrationFactor,        F32,  atof, PM25_CALIBRATION_FACTOR_ADDRESS,        getPM25CalibrationFactor,        setPM25CalibrationFactor)        \
  FIELD(PM25CalibrationOffset,        F32,  atof, PM25_CALIBRATION_OFFSET_ADDRESS,        getPM25CalibrationOffset,        setPM25CalibrationOffset)        \
  FIELD(PM1CalibrationFactor,         F32,  atof, PM1_CALIBRATION_FACTOR_ADDRESS,         getPM1CalibrationFactor,         setPM1CalibrationFactor)         \
  FIELD(PM1CalibrationOffset,         F32,  atof, PM1_CALIBRATION_OFFSET_ADDRESS,         getPM1CalibrationOffset,         setPM1CalibrationOffset)         \
  FIELD(mqttPort,                     U16,  atoi, PUB_SUB_PORT_ADDRESS,                   getMqttPort,                     setMqttPort)                     \
  FIELD(birdhouseNumber,              U16,  atoi, BIRDHOUSE_NUMBER_ADDRESS,               getBirdhouseNumber,              setBirdhouseNumber)              \
  FIELD(sampleDuration,               U16,  atoi, SAMPLE_DURATION_ADDRESS,                getSampleDuration,               setSampleDuration)               \
  FIELD(ledsInstalledBackwards,       bool, atoi, LEDS_INSTALLED_BACKWARDS_ADDRESS,       getLedsInstalledBackwards,       setLedsInstalledBackwards)       \
  FIELD(traditionalLeds,              bool, atoi, TRADITIONAL_LEDS_ADDRESS,               getTraditionalLeds,              setTraditionalLeds)              \

#define STRING_FIELD_LIST \
  FIELD(0, localSsid,       LOCAL_SSID_ADDRESS,       SSID_LENGTH,             getLocalSsid,       setLocalSsid)       \
  FIELD(1, localPassword,   LOCAL_PASSWORD_ADDRESS,   PASSWORD_LENGTH,         getLocalPassword,   setLocalPassword)   \
  FIELD(2, wifiSsid,        WIFI_SSID_ADDRESS,        SSID_LENGTH,             getWifiSsid,        setWifiSsid)        \
  FIELD(3, wifiPassword,    WIFI_PASSWORD_ADDRESS,    PASSWORD_LENGTH,         getWifiPassword,    setWifiPassword)    \
  FIELD(4, deviceToken,     DEVICE_KEY_ADDRESS,       DEVICE_KEY_LENGTH,       getDeviceToken,     setDeviceToken)     \
  FIELD(5, mqttUrl,         MQTT_URL_ADDRESS,         URL_LENGTH,              getMqttUrl,         setMqttUrl)         \
  FIELD(6, firmwareVersion, FIRMWARE_VERSION_ADDRESS, FIRMWARE_VERSION_LENGTH, getFirmwareVersion, setFirmwareVersion) \



private:
  typedef EEPROMClass Parent;


  // The vars we store in EEPROM are declared via these two xmacros:

  // Procuces a block of code that follows this pattern:
  //    F32 temperatureCalibrationFactor;
  #define FIELD(name, datatype, c, d, e, f)             \
    datatype name;
    NUMERIC_FIELD_LIST  
  #undef FIELD


  // Procuces a block of code that follows this pattern:
  //    char localSsid[SSID_LENGTH + 1];
  #define FIELD(a, name, c, length, e, f)  char name[length + 1];
    STRING_FIELD_LIST  
  #undef FIELD

  const char SENTINEL_MARKER[SENTINEL_MARKER_LENGTH + 1] = "SensorBot by Chris Eykamp -- v106";   // Changing this will cause devices to revert to default configuration

// Create a block of code that looks like this:
//     int lengths[] = { 0, sizeof(localSsid), ...};
// constexpr static int lengths[] = { 0
//     #define FIELD(a, name, c, d, e, f) , sizeof(name)
//         STRING_FIELD_LIST
//     #undef FIELD
// };


// constexpr static int totalLength(int index) {
//     return index == lengths[index] ? 0 : (lengths[index] + totalLength(index - 1));
// }


// #define FIELD(index, b, address, d, e, f)     const int address = totalLength(index);
//   STRING_FIELD_LIST  
// #undef FIELD



  // Memory layout:
  const int LOCAL_SSID_ADDRESS                     = 0;  
  const int LOCAL_PASSWORD_ADDRESS                 = LOCAL_SSID_ADDRESS                     + sizeof(localSsid); 
  const int WIFI_SSID_ADDRESS                      = LOCAL_PASSWORD_ADDRESS                 + sizeof(localPassword); 
  const int WIFI_PASSWORD_ADDRESS                  = WIFI_SSID_ADDRESS                      + sizeof(wifiSsid);  
  const int DEVICE_KEY_ADDRESS                     = WIFI_PASSWORD_ADDRESS                  + sizeof(wifiPassword);  
  const int MQTT_URL_ADDRESS                       = DEVICE_KEY_ADDRESS                     + sizeof(deviceToken); 
  const int PUB_SUB_PORT_ADDRESS                   = MQTT_URL_ADDRESS                       + sizeof(mqttUrl); 
  const int SAMPLE_DURATION_ADDRESS                = PUB_SUB_PORT_ADDRESS                   + sizeof(mqttPort);  

  const int BIRDHOUSE_NUMBER_ADDRESS               = SAMPLE_DURATION_ADDRESS                + sizeof(sampleDuration);  


  const int FIRMWARE_VERSION_ADDRESS               = BIRDHOUSE_NUMBER_ADDRESS               + sizeof(birdhouseNumber); 
  const int LEDS_INSTALLED_BACKWARDS_ADDRESS       = FIRMWARE_VERSION_ADDRESS               + sizeof(firmwareVersion);  
  const int TRADITIONAL_LEDS_ADDRESS               = LEDS_INSTALLED_BACKWARDS_ADDRESS       + sizeof(ledsInstalledBackwards); 
  const int TEMPERATURE_CALIBRATION_FACTOR_ADDRESS = TRADITIONAL_LEDS_ADDRESS               + sizeof(traditionalLeds);  
  const int TEMPERATURE_CALIBRATION_OFFSET_ADDRESS = TEMPERATURE_CALIBRATION_FACTOR_ADDRESS + sizeof(temperatureCalibrationFactor);  
  const int HUMIDITY_CALIBRATION_FACTOR_ADDRESS    = TEMPERATURE_CALIBRATION_OFFSET_ADDRESS + sizeof(temperatureCalibrationOffset); 
  const int HUMIDITY_CALIBRATION_OFFSET_ADDRESS    = HUMIDITY_CALIBRATION_FACTOR_ADDRESS    + sizeof(humidityCalibrationFactor); 
  const int PRESURE_CALIBRATION_FACTOR_ADDRESS     = HUMIDITY_CALIBRATION_OFFSET_ADDRESS    + sizeof(humidityCalibrationOffset); 
  const int PRESURE_CALIBRATION_OFFSET_ADDRESS     = PRESURE_CALIBRATION_FACTOR_ADDRESS     + sizeof(pressureCalibrationFactor); 
  const int PM10_CALIBRATION_FACTOR_ADDRESS        = PRESURE_CALIBRATION_OFFSET_ADDRESS     + sizeof(pressureCalibrationOffset); 
  const int PM10_CALIBRATION_OFFSET_ADDRESS        = PM10_CALIBRATION_FACTOR_ADDRESS        + sizeof(PM10CalibrationFactor); 
  const int PM25_CALIBRATION_FACTOR_ADDRESS        = PM10_CALIBRATION_OFFSET_ADDRESS        + sizeof(PM10CalibrationOffset); 
  const int PM25_CALIBRATION_OFFSET_ADDRESS        = PM25_CALIBRATION_FACTOR_ADDRESS        + sizeof(PM25CalibrationFactor); 
  const int PM1_CALIBRATION_FACTOR_ADDRESS         = PM25_CALIBRATION_OFFSET_ADDRESS        + sizeof(PM25CalibrationOffset);
  const int PM1_CALIBRATION_OFFSET_ADDRESS         = PM1_CALIBRATION_FACTOR_ADDRESS         + sizeof(PM1CalibrationFactor);

  const int SENTINEL_ADDRESS        = PM1_CALIBRATION_OFFSET_ADDRESS + sizeof(PM1CalibrationOffset);
  const int NEXT_ADDRESS            = SENTINEL_ADDRESS               + sizeof(SENTINEL_MARKER); 
  const int EEPROM_SIZE = NEXT_ADDRESS;

public:

  void begin() {
    Parent::begin(EEPROM_SIZE);


    // Procuces a block of code that follows this pattern:
    //     readStringFromEeprom(LOCAL_SSID_ADDRESS, sizeof(localSsid) - 1, localSsid);
    #define FIELD(a, name, address, d, e, f)                      \
      readStringFromEeprom(address, sizeof(name) - 1, name);
      STRING_FIELD_LIST  
    #undef FIELD


    // Procuces a block of code that follows this pattern:
    //     temperatureCalibrationFactor = readNumberFromEeprom<F32>(TEMPERATURE_CALIBRATION_FACTOR_ADDRESS);
    #define FIELD(name, datatype, c, address, e, f)             \
      name = readNumberFromEeprom<datatype>(address);
      NUMERIC_FIELD_LIST  
    #undef FIELD
  }


  int getLocalPasswordSize() {
    return sizeof(localPassword);
  }


  // Create a block of code for every item in STRING_FIELD_LIST that looks like this:
  //     void setLocalSsid(const char *ssid) {
  //       copy(localSsid, ssid, sizeof(localSsid) - 1);
  //       writeStringToEeprom(LOCAL_SSID_ADDRESS, sizeof(localSsid) - 1, localSsid);
  //     }
  //
  //     const char *getLocalSsid() {
  //       return localSsid;
  //     }
  #define FIELD(a, name, address, d, getter, setter)               \
    void setter(const char *token) {                               \
      copy(name, token, sizeof(name) - 1);                         \
      writeStringToEeprom(address, sizeof(name) - 1, name);        \
    }                                                              \
                                                                   \
    const char *getter() {                                         \
      return name;                                                 \
    }                                                              \

    STRING_FIELD_LIST  
  #undef FIELD


  // Create a block of code for every item in NUMERIC_FIELD_LIST that looks like this:
  //     void setTemperatureCalibrationFactor(const char *stringifiedParam) {                           
  //         temperatureCalibrationFactor = atof(stringifiedParam);                              
  //         writeNumberToEeprom(TEMPERATURE_CALIBRATION_FACTOR_ADDRESS, temperatureCalibrationFactor);                                 
  //     }                                                                     
  //                                                                           
  //     F32 getTemperatureCalibrationFactor() {                                                   
  //        return temperatureCalibrationFactor;                                                        
  //     }                                                                     
  #define FIELD(name, datatype, fromStringFn, address, getter, setter)    \
    void setter(const char *stringifiedParam) {                           \
        name = fromStringFn(stringifiedParam);                            \
        writeNumberToEeprom(address, name);                               \
    }                                                                     \
                                                                          \
    datatype getter() {                                                   \
       return name;                                                       \
    }                                                                     \

    NUMERIC_FIELD_LIST  
  #undef FIELD




  // void writeSentinelMarker() {
  //   writeStringToEeprom(SENTINEL_ADDRESS, sizeof(SENTINEL_MARKER) - 1, SENTINEL_MARKER);
  // }


  // bool verifySentinelMarker() {
  //   char storedSentinelMarker[SENTINEL_MARKER_LENGTH + 1];
  //   readStringFromEeprom(SENTINEL_ADDRESS, sizeof(storedSentinelMarker) - 1, storedSentinelMarker);

  //   // Return true if the marker is there, false otherwise
  //   return (strcmp(SENTINEL_MARKER, storedSentinelMarker) == 0);
  // }


private:
  // Utility functions

  void writeStringToEeprom(int addr, int length, const char *value) {
    for (int i = 0; i < length; i++)
      write(addr + i, value[i]);
          
    write(addr + length, '\0');
    commit();
  }


  void readStringFromEeprom(int addr, int length, char container[]) {
    for (int i = 0; i < length; i++)
      container[i] = read(addr + i);

    container[length] = '\0';   // Better safe than sorry!
  }


  // This function will read a numeric from addr byte-by-byte and reconstitute into a whole
  template <typename T>
  void writeNumberToEeprom(int addr, T value) {
    byte *p = reinterpret_cast<byte *>(&value);

    for(int i = 0; i < sizeof(T); i++) {
      write(addr + i, p[i]);    // Write the ith byte 
    }

    commit();
  }



  // This function will write a numeric value byte-by-byte to eeprom at the specified address
  template <typename T>
  T readNumberFromEeprom(int addr) {
    T value = 0;
    byte *p = reinterpret_cast<byte *>(&value);

    for(int i = 0; i < sizeof(T); i++) {
      p[i] = read(addr + i);
    }

    return value;
  }


  void copy(char *dest, const char *source, U32 destSize) {
    strncpy(dest, source, destSize);
    dest[destSize] = '\0';
  }



};


BirdhouseEeprom Eeprom;   // Create an instance of our BiridhouseEeprom

#endif