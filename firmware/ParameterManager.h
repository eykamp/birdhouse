#ifndef LIST_SERVER_UTILS_H
#define LIST_SERVER_UTILS_H

#include <vector>
#include <utility>      // For pair
#include <functional>   // For callback support

// We get here when a parameter is passed via the aREST setparams handler
class ParameterManager
{


private:

std::function<void()> localSsidChangedCallback;
std::function<void()> localPasswordChangedCallback;
std::function<void()> wifiCredentialsChangedCallback;
std::function<void()> mqttCredentialsChangedCallback;


public:

void setLocalSsidChangedCallback(std::function<void()> callback) {
  localSsidChangedCallback = callback;
}


void setLocalPasswordChangedCallback(std::function<void()> callback) {
  localPasswordChangedCallback = callback;
}


void setWifiCredentialsChangedCallback(std::function<void()> callback) {
  wifiCredentialsChangedCallback = callback;
}

void setMqttCredentialsChangedCallback(std::function<void()> callback) {
  mqttCredentialsChangedCallback = callback;
}


ParameterManager() {
  localSsidChangedCallback = NULL;
  localPasswordChangedCallback = NULL;
  wifiCredentialsChangedCallback = NULL;
  mqttCredentialsChangedCallback = NULL;
}


int setParam(const String &key, const String &val) {
  if(key.equalsIgnoreCase("ledsInstalledBackwards")) {
    Eeprom.setLedsInstalledBackwards((val[0] == 't' || val[0] == 'T') ? "1" : "0");
  }

  else if(key.equalsIgnoreCase("traditionalLeds"))
    Eeprom.setTraditionalLeds((val[0] == 't' || val[0] == 'T') ? "1" : "0");

  else if(key.equalsIgnoreCase("localSsid"))
  {
    Eeprom.setLocalSsid(val.c_str());

    if(localSsidChangedCallback)
      localSsidChangedCallback();
  }

  else if(key.equalsIgnoreCase("localPass")) {
    if(val.length() < 8 || val.length() > Eeprom.getLocalPasswordSize() - 1)
      return 0;

    Eeprom.setLocalPassword(val.c_str());

    if(localPasswordChangedCallback)
      localPasswordChangedCallback();
  }

  else if(key.equalsIgnoreCase("wifiSsid")) {
    Eeprom.setWifiSsid(val.c_str());

    if(wifiCredentialsChangedCallback) 
      wifiCredentialsChangedCallback();
  }

  else if(key.equalsIgnoreCase("wifiPass")) {
    Eeprom.setWifiPassword(val.c_str());

    if(wifiCredentialsChangedCallback)
      wifiCredentialsChangedCallback();
  }

  else if(key.equalsIgnoreCase("deviceToken"))
    Eeprom.setDeviceToken(val.c_str());

  else if(key.equalsIgnoreCase("mqttUrl")) {
    Eeprom.setMqttUrl(val.c_str());

    if(mqttCredentialsChangedCallback)
        mqttCredentialsChangedCallback();
  }

  else if(key.equalsIgnoreCase("mqttPort")) {
    Eeprom.setMqttPort(val.c_str());

    if(mqttCredentialsChangedCallback)
      mqttCredentialsChangedCallback();
  }

  else if(key.equalsIgnoreCase("sampleDuration")) {
    Eeprom.setSampleDuration(val.c_str());
  }

  else if(key.equalsIgnoreCase("temperatureCalibrationFactor"))
    Eeprom.setTemperatureCalibrationFactor(val.c_str());

  else if(key.equalsIgnoreCase("temperatureCalibrationOffset"))
    Eeprom.setTemperatureCalibrationOffset(val.c_str());

  else if(key.equalsIgnoreCase("humidityCalibrationFactor"))
    Eeprom.setHumidityCalibrationFactor(val.c_str());

  else if(key.equalsIgnoreCase("humidityCalibrationOffset"))
    Eeprom.setHumidityCalibrationOffset(val.c_str());

  else if(key.equalsIgnoreCase("pressureCalibrationFactor"))
    Eeprom.setPressureCalibrationFactor(val.c_str());

  else if(key.equalsIgnoreCase("pressureCalibrationOffset"))
    Eeprom.setPressureCalibrationOffset(val.c_str());

  else if(key.equalsIgnoreCase("PM10CalibrationFactor"))
    Eeprom.setPM10CalibrationFactor(val.c_str());

  else if(key.equalsIgnoreCase("PM10CalibrationOffset"))
    Eeprom.setPM10CalibrationOffset(val.c_str());

  else if(key.equalsIgnoreCase("PM25CalibrationFactor"))
    Eeprom.setPM25CalibrationFactor(val.c_str());

  else if(key.equalsIgnoreCase("PM25CalibrationOffset"))
    Eeprom.setPM25CalibrationOffset(val.c_str());

  else if(key.equalsIgnoreCase("PM1CalibrationFactor"))
    Eeprom.setPM1CalibrationFactor(val.c_str());

  else if(key.equalsIgnoreCase("PM1CalibrationOffset"))
    Eeprom.setPM1CalibrationOffset(val.c_str());

  return 1;
}


// This function gets registered with aREST, or pass it a string like "mqttUrl=http://sensorbot.org&mqttPort=8989"
int setParamsHandler(const String &params) {
  std::vector<std::pair<String,String>> kvPairs;
  parse(params, kvPairs);

  for(int i = 0; i < kvPairs.size(); i++)
    setParam(kvPairs[i].first, kvPairs[i].second);

  return 1;
}


private:
// Helpers

// Modifies tokens
static void split(const String &str, const String &delim, std::vector<String> &tokens) {
  auto start = 0U;
  auto end = str.indexOf(delim);
  while (end != -1)
  {
    tokens.push_back(str.substring(start, end));
    start = end + delim.length();
    end = str.indexOf(delim, start);
  }

  tokens.push_back(str.substring(start));
}


// Modifies kv 
static bool split(const String &str, const String &delim, std::pair<String, String> &kv) {
  auto pos = str.indexOf(delim);
  if(pos == -1)
    return false;

  kv.first = str.substring(0, pos);
  kv.second = str.substring(pos + 1);

  return true;
}


public:
// Modifies kvPairs
static void parse(const String &params, std::vector<std::pair<String,String>> &kvPairs) {
  std::vector<String> tokens;
  split(params, "&", tokens);

  std::pair<String, String> kv;
  for(int i = 0; i < tokens.size(); i++) {
    if(split(tokens[i], "=", kv))
      kvPairs.push_back(kv);
  }
}



};


#endif