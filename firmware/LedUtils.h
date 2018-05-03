#ifndef LED_UTILS_H
#define LED_UTILS_H

#include "ParameterManager.h"
#include "Intervals.h"

#include <Adafruit_DotStar.h>

class LedUtils {
public:

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


private:

#define BLINK_STATES 2
U8 blinkColor[BLINK_STATES] = { NONE, NONE };

U8 blinkTime = 24 * HOURS;
U8 blinkState = 0;
U32 blinkTimer = 0;

U8 redLedPin;
U8 yellowLedPin;
U8 greenLedPin;
U8 builtinLedPin;

BlinkPattern blinkPattern = STARTUP;

ParameterManager::LedStyle ledStyle = ParameterManager::RYG;


Adafruit_DotStar strip;  // We'll never acutally use this when traditional LEDs are installed, but we don't know that at compile time, and instantiation isn't terribly harmful


public:

LedUtils() : 
  strip(Adafruit_DotStar(1, 0, 1, DOTSTAR_BGR)) {
}


void begin(U8 redPin, U8 yellowPin, U8 greenPin, U8 builtinPin, U8 dotStarDataPin, U8 dotStarClockPin) {
  redLedPin     = redPin;
  yellowLedPin  = yellowPin;
  greenLedPin   = greenPin;
  builtinLedPin = builtinPin;

  pinMode(redLedPin,     OUTPUT);
  pinMode(yellowLedPin,  OUTPUT);
  pinMode(greenLedPin,   OUTPUT);
  pinMode(builtinLedPin, OUTPUT);

  strip.updatePins(dotStarDataPin, dotStarClockPin);
}


void setLedStyle(ParameterManager::LedStyle style) {
  ledStyle = style;
}


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


void setBlinkPatternByName(const char *mode) {
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


void loop() {
  if(millis() - blinkTimer < blinkTime)
    return;

  blinkTimer = millis();
  blinkState++;
  if(blinkState >= BLINK_STATES)
    blinkState = 0;

  activateLed(blinkColor[blinkState]);
}


void playStartupSequence() {
  activateLed(RED);
  delay(300);
  activateLed(YELLOW);
  delay(300);
  activateLed(GREEN);
  delay(300);
  activateLed(NONE);
}


private:

bool getLowState() {
  return (ledStyle == ParameterManager::RYG_REVERSED) ? HIGH : LOW;
}

bool getHighState() {
  return (ledStyle == ParameterManager::RYG_REVERSED) ? LOW : HIGH;
}


void activateLed(U32 ledMask) {
  if(ledStyle == ParameterManager::RYG || ledStyle == ParameterManager::RYG_REVERSED) {

    digitalWrite(redLedPin,     (ledMask & RED)     ? getHighState() : getLowState());
    digitalWrite(yellowLedPin,  (ledMask & YELLOW)  ? getHighState() : getLowState());
    digitalWrite(greenLedPin,   (ledMask & GREEN)   ? getHighState() : getLowState());
    digitalWrite(builtinLedPin, (ledMask & BUILTIN) ? LOW : HIGH);    // builtin uses reverse states
  } 
  else if(ledStyle == ParameterManager::DOTSTAR) {

    int red   = (ledMask & (RED | YELLOW   )) ? 255 : 0;
    int green = (ledMask & (YELLOW | GREEN )) ? 255 : 0;
    int blue  = (ledMask & (0         )) ? 255 : 0;

    strip.setPixelColor(0, red, green, blue);
    strip.show(); 
  }
  else if(ledStyle == ParameterManager::FOUR_PIN_COMMON_ANNODE) {
    // Do something!
  }
}

};


LedUtils ledUtils;

#endif

