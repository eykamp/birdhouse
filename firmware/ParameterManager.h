#ifndef LIST_SERVER_UTILS_H
#define LIST_SERVER_UTILS_H

#include <vector>
#include <utility>      // For pair
#include <functional>   // For callback support

// We get here when a parameter is passed via the aREST setparams handler
class ParameterManager
{

public:

// Don't remove items from here -- we store values in the eeprom by enum value/number, and removing an unused entry will shift all higher values, causing mayhem.
// Rather than delete, mark an item as unused and ignore it.
//
// If we change any of the values in the second column, we need to modify configurator.py
#define LED_STYLE_TABLE \
  LED_STYLE(RYG,                    "RYG",          "3 LEDs")                                     \
  LED_STYLE(RYG_REVERSED,           "RYG_REVERSED", "3 LEDs, reverse wired")                      \
  LED_STYLE(DOTSTAR,                "DOTSTAR",      "Dotstar multicolor LED")                     \
  LED_STYLE(FOUR_PIN_COMMON_ANNODE, "4PIN",         "4-pin multicolor LED with common annode")    \



// Procuces an enum of LED styles
enum LedStyle {  

#define LED_STYLE(enumval, b, c)  enumval,
  LED_STYLE_TABLE
#undef LED_STYLE

  STYLE_COUNT,
  UNKNOWN_STYLE
};


private:

std::function<void()> localCredentialsChangedCallback;
std::function<void()> wifiCredentialsChangedCallback;
std::function<void()> mqttCredentialsChangedCallback;


public:

void setLocalCredentialsChangedCallback(std::function<void()> callback) {
  localCredentialsChangedCallback = callback;
}


void setWifiCredentialsChangedCallback(std::function<void()> callback) {
  wifiCredentialsChangedCallback = callback;
}

void setMqttCredentialsChangedCallback(std::function<void()> callback) {
  mqttCredentialsChangedCallback = callback;
}


ParameterManager() {
  localCredentialsChangedCallback = NULL;
  wifiCredentialsChangedCallback = NULL;
  mqttCredentialsChangedCallback = NULL;
}


int setParam(const String &key, const String &val) {
  if(key.equalsIgnoreCase("ledStyle")) {
    LedStyle style = getLedStyleFromName(val);
    if(style != UNKNOWN_STYLE)
      Eeprom.setLedStyle(String(style).c_str());
  }

  else if(key.equalsIgnoreCase("localSsid"))
  {
    Eeprom.setLocalSsid(val.c_str());

    if(localCredentialsChangedCallback)
      localCredentialsChangedCallback();
  }

  else if(key.equalsIgnoreCase("localPass")) {
    if(val.length() < 8 || val.length() > Eeprom.getLocalPasswordSize() - 1)
      return 0;

    Eeprom.setLocalPassword(val.c_str());

    if(localCredentialsChangedCallback)
      localCredentialsChangedCallback();
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

  else if(key.equalsIgnoreCase("serialNumber")) {
    Eeprom.setBirdhouseNumber(val.c_str());
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
    if(split(tokens[i], "=", kv)) {
      kvPairs.push_back(kv);
    }
  }
}


static const char *getLedStyleName(LedStyle style) {
  // Use dummy statement to let us use "else if" on all items below
  if(false)
    return "";

  // Creates a block of else if statements that look like this:
  // else if(style == RYG)
  //    return "RYG";
  #define LED_STYLE(enumval, styleName, c)    \
    else if(style == enumval)                 \
      return styleName;
    LED_STYLE_TABLE
  #undef LED_STYLE

  else
    return "Unknown";
}


static const char *getLedStyleDescription(LedStyle style) {
  // Use dummy statement to let us use "else if" on all items below
  if(false)
    return "";

  // Creates a block of else if statements that look like this:
  // else if(style == RYG)
  //    return "RYG";
  #define LED_STYLE(enumval, b, descr)        \
    else if(style == enumval)                 \
      return descr;
    LED_STYLE_TABLE
  #undef LED_STYLE

  else
    return "No description";
}


static LedStyle getLedStyleFromName(const String &name) {
  // Use dummy statement to let us use "else if" on all items below
  if(false)
    return UNKNOWN_STYLE;

  // Creates a block of else if statements that look like this:
  // else if(name.equalsIgnoreCase("RYG"))
  //    return RYG;
  #define LED_STYLE(enumval, styleName, c)        \
    else if(name.equalsIgnoreCase(styleName))     \
      return enumval;
    LED_STYLE_TABLE
  #undef LED_STYLE

  else
    return UNKNOWN_STYLE;
}





};


#endif