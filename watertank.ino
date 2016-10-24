/*
  NAME:
  Observing water tank, rain intensity, and ambient light intensity.

  DESCRIPTION:
  Application for observing water tank, rain intensity, and ambient light
  intensity as well as boot count and RSSI.
  - The application reads from various sensors and publishes gained values
    in order to observe the cottage garden.
  - Observation sensors:
    - Water level in water tank by ultrasonic sersor HC-SR04 in centimeters
      within range 2 ~ 90 cm with resolution 1 cm.
    - Rain intensity by analog resistive sensor board YL-83 in bits within
      range 0 ~ 4095 with 12-bit resolution.
    - Ambient light intensity by analog sensor TEMT6000 in bits within
      range 0 ~ 4095 with 12-bit resolution.
  - System observation:
    - Number of boots since recent power-up.
    - RSSI wifi signal intensity.
  - All measured values are statistically processed:
    - A batch (burst) of measured values is statistically smoothed by median
      and result is considered as a measured value.
    - Smoothed (measured) values is then processed by exponential filter
      with individual smoothing factor and result is considered as a final
      processed value.      
  - Hardware watchdog timers built in microcontroller are utilized for
    reseting the microcontroller when it freezes in order to ensure
    unattended operation.  
  - Publishing to cloud services:
    - ThingSpeak for graphing and further analyzing of smoothed
      and filtered values.
    - Blynk for publishing smoothed and filtered values in mobile
      application. 
  - The application utilizes a separate include credentials file
    with credentials to cloud services.
    - The credential file contains just placeholder for credentials.
    - Replace credential placeholders with real ones only in the Particle
      dashboard in order not to share or reveal them.

  LICENSE:
  This program is free software; you can redistribute it and/or modify
  it under the terms of the MIT License (MIT).

  CREDENTIALS:
  Author: Libor Gabaj
*/
// #define PHOTON_PUBLISH_DEBUG      // This publishes debug events to the particle cloud
// #define PHOTON_PUBLISH_VALUE      // This publishes regular events to the particle cloud
// #define BLYNK_DEBUG               // Optional, this enables lots of prints    
// #define BLYNK_PRINT Serial

// Libraries
#include "watchdogs/watchdogs.h"
#include "smooth-sensor-data/smooth-sensor-data.h"
#include "exponential-filter/exponential-filter.h"
#include "sonar-ping/sonar-ping.h"
#include "ThingSpeak/ThingSpeak.h"
#include "blynk/blynk.h"

// Boot
STARTUP(WiFi.selectAntenna(ANT_EXTERNAL));
// STARTUP(WiFi.selectAntenna(ANT_INTERNAL));
STARTUP(System.enableFeature(FEATURE_RETAINED_MEMORY));

//------------------------------------------------------------------------
// Water tank sensing and publishing to clouds (ThinkSpeak, Blynk)
//-------------------------------------------------------------------------
#define SKETCH "WATERTANK 1.0.0"
#include "credentials.h"

const unsigned int COEF_PUBLISH = 12;         // The multiplier of measurements for publishing 
const unsigned int PERIOD_MEASURE = 5000;     // Time between measurements in milliseconds
const unsigned int TIMEOUT_WATCHDOG = 10000;  // Watchdog timeout in milliseconds
const unsigned int REF_VOLTAGE = 3300;        // Reference voltage for analog reading in millivolts

// Hardware configuration
const unsigned char PIN_LIGHT   = A0;    // Ambient light sensor TEMT6000
const unsigned char PIN_RAIN    = A1;    // Rain sensor analog YL-83
const unsigned char PIN_SONIC_T = D1;    // Ultrasonic sensor HC-SR04 trigger
const unsigned char PIN_SONIC_E = D2;    // Ultrasonic sensor HC-SR04 echo

// ThingSpeak
const char* THINGSPEAK_TOKEN = CREDENTIALS_THINGSPEAK_TOKEN;
const unsigned long CHANNEL_NUMBER = CREDENTIALS_THINGSPEAK_CHANNEL;
const unsigned char FIELD_LIGHT = 1;
const unsigned char FIELD_RAIN  = 2;
const unsigned char FIELD_WATER = 3;
const unsigned char FIELD_RSSI  = 4;
TCPClient ThingSpeakClient;

// Blynk (Virtual pins V1, V2, V3 used for Photon-Mierova)
const char* BLYNK_TOKEN = CREDENTIALS_BLYNK_TOKEN;
#define VPIN_BOOT  V4     // Virtual pin for boot count
#define VPIN_RSSI  V5     // Virtual pin for RSSI
#define VPIN_LIGHT V6     // Virtual pin for light level
#define VPIN_RAIN  V7     // Virtual pin for rain level
#define VPIN_WATER V8     // Virtual pin for water level

// Measured values
retained int bootCount, bootTimeLast, bootRunPeriod;
int rssiValue, lightValue, rainValue, waterValue;

// Statistical smoothing and exponential filtering
const float FACTOR_RSSI  = 0.1;    // Smoothing factor for RSSI
const float FACTOR_LIGHT = 0.8;    // Smoothing factor for light level
const float FACTOR_RAIN  = 0.2;    // Smoothing factor for rain level
const float FACTOR_WATER = 0.2;    // Smoothing factor for water level
ExponentialFilter efRSSI  = ExponentialFilter(FACTOR_RSSI);
ExponentialFilter efLight = ExponentialFilter(FACTOR_LIGHT);
ExponentialFilter efRain  = ExponentialFilter(FACTOR_RAIN);
ExponentialFilter efWater = ExponentialFilter(FACTOR_WATER);
SmoothSensorData smooth;

// Water level
const unsigned int SONAR_DISTANCE_MAX = 95;   // Maximal valid measured distance (to tank bottom)
const unsigned int WATER_TANK_EMPTY = 89;     // Minimal water level (empty tank - to the bottom)
const unsigned int WATER_TANK_FULL  = 3;      // Maximal water level (full tank - to the brink)
SonarPing sonar(PIN_SONIC_T, PIN_SONIC_E, SONAR_DISTANCE_MAX);

void setup() {
    // Boot process
    if (bootCount++ > 0) bootRunPeriod = Time.now() - bootTimeLast;
    bootTimeLast = Time.now();
    Particle.publish("Boot_Cnt_Run", String::format("%d/%d", bootCount, bootRunPeriod));
#ifdef PHOTON_PUBLISH_DEBUG
    Particle.publish("Sketch", String(SKETCH));
#endif
    // Clouds
    ThingSpeak.begin(ThingSpeakClient);
    Blynk.begin(BLYNK_TOKEN);
    // Start watchdogs
    Watchdogs::begin(TIMEOUT_WATCHDOG);  
}

void loop() {
    Watchdogs::tickle();
    Blynk.run();
    measure();
}

void measure() {
  static unsigned long tsMeasure = millis();
  if (millis() - tsMeasure >= PERIOD_MEASURE) {
    tsMeasure = millis();
    // RSSI
    while(smooth.registerData(abs(WiFi.RSSI())));
    rssiValue = efRSSI.getValue(-1 * smooth.getMedian());
    ThingSpeak.setField(FIELD_RSSI, rssiValue);
    // Light
    while(smooth.registerData(analogRead(PIN_LIGHT)));
    lightValue = efLight.getValue(smooth.getMedian());
    ThingSpeak.setField(FIELD_LIGHT, lightValue);
    // Rain
    while(smooth.registerData(analogRead(PIN_RAIN)));
    rainValue = efRain.getValue(smooth.getMedian());
    ThingSpeak.setField(FIELD_RAIN, rainValue);
    // Water
    while (smooth.registerData(sonar.getDistance()));
    waterValue = efWater.getValue(smooth.getMedian());
    ThingSpeak.setField(FIELD_WATER, waterValue);    
    // Publish results
    publish();
  }
}

void publish() {
  static unsigned char cntMeasure = 0;
  if (++cntMeasure >= COEF_PUBLISH) {
    cntMeasure = 0;
    int result = ThingSpeak.writeFields(CHANNEL_NUMBER, THINGSPEAK_TOKEN);
#ifdef PHOTON_PUBLISH_DEBUG
    Particle.publish("ThingSpeakResult", String(result));
#endif
#ifdef PHOTON_PUBLISH_VALUE
    Particle.publish("RSSI/Light/Rain/Water", String::format("%4d/%4d/%4d/%4d", rssiValue, lightValue, rainValue, waterValue));
#endif
  }
}

// Frequency reading in Blynk mobile application
BLYNK_READ(VPIN_BOOT)
{
    Blynk.virtualWrite(VPIN_BOOT, bootCount);
}
BLYNK_READ(VPIN_RSSI)
{
    Blynk.virtualWrite(VPIN_RSSI, rssiValue);
}
BLYNK_READ(VPIN_LIGHT)
{
    Blynk.virtualWrite(VPIN_LIGHT, lightValue);
}
BLYNK_READ(VPIN_RAIN)
{
    Blynk.virtualWrite(VPIN_RAIN, rainValue);
}
BLYNK_READ(VPIN_WATER)
{
    Blynk.virtualWrite(VPIN_WATER, WATER_TANK_EMPTY - waterValue);
}
