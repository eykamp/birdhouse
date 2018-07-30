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
  SOLID_RED,
  SOLID_YELLOW,
  SLOW_BLINK_YELLOW,
  SLOW_BLINK_GREEN,

  WIFI_CONNECT_TIMED_OUT,
  DISCONNECTED_FROM_WIFI,
  CONNECTED_TO_WIFI,
  WIFI_CONNECT_FAILED,
  CONNECTED_TO_MQTT_SERVER,

  BLINK_PATTERN_COUNT
};


private:

#define BLINK_STATES 2

U8 blinkColor[BLINK_STATES] = { NONE, NONE };

U32 blinkTime = 24 * HOURS;
U8 blinkState = 0;
U8 maxBlinkState = 0;
U32 blinkTimer = 0;
bool fading = false;


// Used for flashing patterns
U8 blinkRhythm = 0;   
S8 blinkRhythmCounter = 0;
bool blinkToggle = false;


U8 redLedPin;
U8 yellowLedPin;
U8 greenLedPin;
U8 builtinLedPin;

BlinkPattern blinkPattern = OFF;

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

  // Start with all LEDs off
  digitalWrite(redLedPin,    getLowState());
  digitalWrite(yellowLedPin, getLowState());
  digitalWrite(greenLedPin,  getLowState());

  strip.setPixelColor(0, 0, 0, 0);
  strip.show(); 
}


void setLedStyle(ParameterManager::LedStyle style) {
  ledStyle = style;
}


void setBlinkPattern(BlinkPattern blinkPattern) {
  switch(blinkPattern) {
    case OFF:
      setSolidColor(NONE, false);
      break;


    case WIFI_CONNECT_FAILED:
      if(ledStyle == ParameterManager::BUILTIN_ONLY)
        setBlinkRhythm(5);
      else
        setSlowBlink(RED);
      break;

    case WIFI_CONNECT_TIMED_OUT:
      if(ledStyle == ParameterManager::BUILTIN_ONLY)
        setBlinkRhythm(1);
      else
        setFastBlink(RED);
      break;

    case DISCONNECTED_FROM_WIFI:
      if(ledStyle == ParameterManager::BUILTIN_ONLY)
        setBlinkRhythm(4);
      else
        setFastBlink(YELLOW);
      break;

    case CONNECTED_TO_WIFI:
      if(ledStyle == ParameterManager::BUILTIN_ONLY)
        setBlinkRhythm(2);
      else
        setFastBlink(GREEN);
      break;

    case CONNECTED_TO_MQTT_SERVER:
      if(ledStyle == ParameterManager::BUILTIN_ONLY)
        setBlinkRhythm(0);
      else
        setSolidColor(GREEN, true);
      break;

    case SOLID_RED:
      setSolidColor(RED, false);
      break;

    case SOLID_YELLOW:
      setSolidColor(YELLOW, false);
      break;

    case SLOW_BLINK_YELLOW:
      setSlowBlink(YELLOW);
      break;

    case SLOW_BLINK_GREEN:
      setSlowBlink(GREEN);
      break;
  }
}


void setBlinkPatternByName(const char *mode) {
  if(     strcmp(mode, "OFF")                      == 0)
    setBlinkPattern(OFF);
  else if(strcmp(mode, "SOLID_RED")                == 0)
    setBlinkPattern(SOLID_RED);
  else if(strcmp(mode, "SOLID_YELLOW")             == 0)
    setBlinkPattern(SOLID_YELLOW);
  else if(strcmp(mode, "WIFI_CONNECT_TIMED_OUT")   == 0)
    setBlinkPattern(WIFI_CONNECT_TIMED_OUT);
  else if(strcmp(mode, "DISCONNECTED_FROM_WIFI")   == 0)
    setBlinkPattern(DISCONNECTED_FROM_WIFI);
  else if(strcmp(mode, "CONNECTED_TO_WIFI")        == 0)
    setBlinkPattern(CONNECTED_TO_WIFI);
  else if(strcmp(mode, "CONNECTED_TO_MQTT_SERVER") == 0)
    setBlinkPattern(CONNECTED_TO_MQTT_SERVER);
  else if(strcmp(mode, "WIFI_CONNECT_FAILED")      == 0)
    setBlinkPattern(WIFI_CONNECT_FAILED);
  else if(strcmp(mode, "SLOW_BLINK_YELLOW")        == 0)
    setBlinkPattern(SLOW_BLINK_YELLOW);
  else if(strcmp(mode, "SLOW_BLINK_GREEN")         == 0)
    setBlinkPattern(SLOW_BLINK_GREEN);
}


U32 activeLedColorMask;

void loop() {

  if(millis() - blinkTimer > blinkTime) {     // Time to advance to the next blinkstate?
    blinkTimer = millis();

    if(ledStyle == ParameterManager::BUILTIN_ONLY) {

      if(blinkRhythm == 0)
        activeLedColorMask = BUILTIN;
        
      else {
        blinkToggle = !blinkToggle;

        if(blinkToggle) {
          blinkRhythmCounter++;
          if(blinkRhythmCounter > blinkRhythm) {
            blinkRhythmCounter = 0;
            blinkTime = 1 * SECONDS;
          }
          else 
            blinkTime = 200 * MILLIS;
        }
        activeLedColorMask = (blinkToggle && blinkRhythmCounter > 0) ? BUILTIN : NONE;
      }
    } 
    else {    // Most cases
      blinkState++;
     
      if(blinkState > maxBlinkState)
        blinkState = 0;

      activeLedColorMask = blinkColor[blinkState];
    }
  }

  activateLed(activeLedColorMask);
}


// Adapted from https://www.instructables.com/topics/linear-PWM-LED-fade-with-arduino/
// Returns a pwm value (0..1001) for a required percentage (0..100) to provide a linear fade as perceived by eye
int linearPWM(int percentage) {
  // coefficients
  double a = 9.7758463166360387E-01;
  double b = 5.5498961535023345E-02;

  // When input ranges from 0 to 100, this computes a value between 1 and 1001 that provides smooth pwm values
  F32 amt = floor(((a * exp(b * percentage) + .5)) - 1)  * 4 + 1;   

  return amt;   
}


void playStartupSequence() {
  if(ledStyle == ParameterManager::BUILTIN_ONLY) {
    // 8 rapid flashes
    for(int i = 0; i < 8; i++) {
      activateLed(BUILTIN);
      delay(100);
      activateLed(NONE);
      delay(100);
    }
  }

  else {
    // Quick cycle of red/yellow/green
    activateLed(RED);
    delay(300);
    activateLed(YELLOW);
    delay(300);
    activateLed(GREEN);
    delay(300);
    activateLed(NONE);
  }
}



private:

void setBlinkRhythm(U8 blinkCount) {
  blinkRhythm = blinkCount;
  blinkRhythmCounter = -1;
  blinkToggle = true;
  blinkTime = 1 * SECONDS;

  fading = (blinkCount == 0);
}


void setFastBlink(Leds color) {
  blinkColor[0] = color;
  blinkColor[1] = NONE;
  blinkTime = 400 * MILLIS;
  fading = false;
  maxBlinkState = 1;
}


void setSlowBlink(Leds color) {
  blinkColor[0] = color;
  blinkColor[1] = NONE;
  blinkTime = 1 * SECONDS;
  fading = false;
  maxBlinkState = 1;
}


void setSolidColor(Leds color, bool fad) {
  blinkColor[0] = color;
  blinkColor[1] = NONE;
  blinkTime = 24 * HOURS;
  maxBlinkState = 0;
  fading = fad;
}


bool getLowState() {
  return (ledStyle == ParameterManager::RYG_REVERSED || ledStyle == ParameterManager::BUILTIN_ONLY) ? HIGH : LOW;
}


bool getHighState() {
  return (ledStyle == ParameterManager::RYG_REVERSED || ledStyle == ParameterManager::BUILTIN_ONLY) ? LOW : HIGH;
}


// Returns a number between 0 and 100 following a triagle pattern
int getTriangleValue() {
  int max = (ledStyle == ParameterManager::BUILTIN_ONLY) ? 100 : 50;   // Don't go higher than 100 here!
  F32 duration = 2.5 * SECONDS; 

  return triangle(max, duration);  
}


void activateLed(U32 ledMask) {
  if(ledStyle == ParameterManager::RYG || ledStyle == ParameterManager::RYG_REVERSED) {

    if(fading) {
      int pwm = linearPWM(getTriangleValue());

      analogWrite(redLedPin,     ( (ledMask & RED))     ? pwm : 0);
      analogWrite(yellowLedPin,  ( (ledMask & YELLOW))  ? pwm : 0);
      analogWrite(greenLedPin,   ( (ledMask & GREEN))   ? pwm : 0);
      analogWrite(builtinLedPin, ( (ledMask & BUILTIN)) ? pwm : 0);
    } 

    else {
      digitalWrite(redLedPin,     (ledMask & RED)     ? getHighState() : getLowState());
      digitalWrite(yellowLedPin,  (ledMask & YELLOW)  ? getHighState() : getLowState());
      digitalWrite(greenLedPin,   (ledMask & GREEN)   ? getHighState() : getLowState());
      digitalWrite(builtinLedPin, (ledMask & BUILTIN) ? LOW : HIGH);    // builtin uses reverse states
    }
  }

  else if(ledStyle == ParameterManager::DOTSTAR) {

    int red   = (ledMask & (RED | YELLOW   )) ? 255 : 0;
    int green = (ledMask & (YELLOW | GREEN )) ? 255 : 0;
    int blue  = (ledMask & (0              )) ? 255 : 0;


    if(fading) {
      F32 blackening = F32(getTriangleValue()) / 100.0;

      red   *= blackening;
      green *= blackening;
      blue  *= blackening;
    }

    strip.setPixelColor(0, red, green, blue);
    strip.show(); 
  }

  else if(ledStyle == ParameterManager::FOUR_PIN_COMMON_ANNODE) {
    // With common annode LEDs, LOW is on, HIGH is off; similarly, pwm 0 is on, 1023 (value defined by ESP8266 PWMRANGE macro) is off
    if(fading) {
      int pwm = linearPWM(getTriangleValue());

      analogWrite(redLedPin,     (ledMask & RED   || ledMask & YELLOW)  ? PWMRANGE - pwm : PWMRANGE);
      analogWrite(yellowLedPin,  (ledMask & GREEN || ledMask & YELLOW)  ? PWMRANGE - pwm : PWMRANGE);
      digitalWrite(greenLedPin,   getHighState());
    } else {
      digitalWrite(redLedPin,     (ledMask & RED   || ledMask & YELLOW)  ? LOW : HIGH);
      digitalWrite(yellowLedPin,  (ledMask & GREEN || ledMask & YELLOW)  ? LOW : HIGH);
      digitalWrite(greenLedPin,   getHighState());
    }
  }

  else if(ledStyle == ParameterManager::BUILTIN_ONLY) {
    if(fading) {
      int pwm = linearPWM(getTriangleValue());
      analogWrite(builtinLedPin, (ledMask & BUILTIN) ? PWMRANGE - pwm : PWMRANGE);
    } else {
      digitalWrite(builtinLedPin, (ledMask & BUILTIN) ? getHighState() : getLowState());
    }
  }
}


// Generates triangle wave between 0 and range, with specified period
// Adapted from // https://stackoverflow.com/questions/1073606/is-there-a-one-line-function-that-generates-a-triangle-wave
int triangle(int range, int period) {
    U32 x = millis();
    return (F32(range) / F32(period)) * (period - abs(x % (2 * period) - period));     
  }


};


LedUtils ledUtils;

#endif

