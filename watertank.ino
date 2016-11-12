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
      within range 2 ~ 95 cm with resolution 1 cm.
    - Rain intensity by analog resistive sensor board YL-83 in bits within
      range 0 ~ 4095 with 12-bit resolution.
    - Ambient light intensity by analog sensor TEMT6000 in bits within
      range 0 ~ 4095 with 12-bit resolution.
    - For each sensor the trend is calculated in units change per minute.
    - For each sensor the status is determined at trend calculation.
    - For each sensor the long term minimal and maximal value in units is
      calculated.
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
    - Blynk for publishing smoothed and filtered values, trends, statuses,
      and minimal and maximal values in mobile application as well as for
      push notifications and led signaling at status changes.
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
// #define BLYNK_NOTIFY_LIGHT
#define BLYNK_NOTIFY_RAIN
#define BLYNK_NOTIFY_WATER

// Libraries
#include "watchdogs/watchdogs.h"
#include "smooth-sensor-data/smooth-sensor-data.h"
#include "exponential-filter/exponential-filter.h"
#include "sonar-ping/sonar-ping.h"
#include "ThingSpeak/ThingSpeak.h"
#include "blynk/blynk.h"
#include "math.h"

// Boot
STARTUP(WiFi.selectAntenna(ANT_EXTERNAL));
// STARTUP(WiFi.selectAntenna(ANT_INTERNAL));
STARTUP(System.enableFeature(FEATURE_RETAINED_MEMORY));

//------------------------------------------------------------------------
// Water tank sensing and publishing to clouds (ThinkSpeak, Blynk)
//-------------------------------------------------------------------------
#define SKETCH "WATERTANK 1.3.0"
#include "credentials.h"

const unsigned int TIMEOUT_WATCHDOG = 10000;  // Watchdog timeout in milliseconds
const unsigned int REF_VOLTAGE = 3300;        // Reference voltage for analog reading in millivolts

const unsigned int COEF_PUBLISH = 12;         // The multiplier of measurements for publishing
const unsigned int PERIOD_MEASURE = 5000;     // Time between measurements in milliseconds

// Measuring periods (delays) in milliseconds
// Count as multiplier for calculating trends
const unsigned int PERIOD_MEASURE_RSSI = 30000;
//
const unsigned int PERIOD_MEASURE_LIGHT = 30000;
const unsigned int COUNT_TREND_LIGHT = 10;
//
const unsigned int PERIOD_MEASURE_RAIN = 5000;
const unsigned int COUNT_TREND_RAIN = 6;
//
const unsigned int PERIOD_MEASURE_WATER = 5000;
const unsigned int COUNT_TREND_WATER = 12;

// Publishing periods (delays) in milliseconds
const unsigned int PERIOD_PUBLISH_PARTICLE = 15000;
const unsigned int PERIOD_PUBLISH_THINGSPEAK = 60000;

// Hardware configuration
const unsigned char PIN_LIGHT   = A0;    // Ambient light sensor TEMT6000
const unsigned char PIN_RAIN    = A1;    // Rain sensor analog YL-83
const unsigned char PIN_SONIC_T = D1;    // Ultrasonic sensor HC-SR04 trigger
const unsigned char PIN_SONIC_E = D2;    // Ultrasonic sensor HC-SR04 echo

// ThingSpeak
const char* THINGSPEAK_TOKEN = CREDENTIALS_THINGSPEAK_TOKEN;
const unsigned long CHANNEL_NUMBER = CREDENTIALS_THINGSPEAK_CHANNEL;
// #define FIELD_RSSI_VALUE  4
#define FIELD_LIGHT_VALUE 1
#define FIELD_LIGHT_TREND 6
#define FIELD_RAIN_VALUE  2
#define FIELD_RAIN_TREND  5
#define FIELD_WATER_VALUE 3
#define FIELD_WATER_TREND 4
//
TCPClient ThingSpeakClient;

// Blynk
const char* BLYNK_TOKEN = CREDENTIALS_BLYNK_TOKEN;
#define VPIN_BOOT_VALUE  V1     // Virtual pin for boot count
#define VPIN_RSSI_VALUE  V2
//
#define VPIN_LIGHT_VALUE V3
#define VPIN_LIGHT_TREND V4
#define VPIN_LIGHT_MIN   V5
#define VPIN_LIGHT_MAX   V6
//
#define VPIN_RAIN_VALUE  V7
#define VPIN_RAIN_TREND  V8
#define VPIN_RAIN_MIN    V9
#define VPIN_RAIN_MAX    V10
//
#define VPIN_WATER_VALUE V11
#define VPIN_WATER_TREND V12
#define VPIN_WATER_MIN   V13
#define VPIN_WATER_MAX   V14
#define VPIN_WATER_PUMP  V15
// Blynk variables
WidgetLED ledWaterPump(VPIN_WATER_PUMP);
#if defined(BLYNK_NOTIFY_LIGHT) || defined(BLYNK_NOTIFY_RAIN) || defined(BLYNK_NOTIFY_WATER)
String BLYNK_LABEL = String("Chalupa -- ");
#endif

// Measured values
int rssiValue;
int lightValue, rainValue, waterValue;
float lightTrend, rainTrend, waterTrend;

// Backup variables (long terms statistics)
retained int bootCount, bootTimeLast, bootRunPeriod;
retained int lightValueMin = 4096, lightValueMax;
retained int rainValueMin = 4096, rainValueMax;
retained int waterValueMin = 100, waterValueMax;
retained unsigned char lightStatus, rainStatus, waterStatus;

// Statistical smoothing and exponential filtering
const float FACTOR_RSSI  = 0.1;    // Smoothing factor for RSSI
const float FACTOR_LIGHT = 0.2;    // Smoothing factor for light level
const float FACTOR_RAIN  = 0.5;    // Smoothing factor for rain level
const float FACTOR_WATER = 0.8;    // Smoothing factor for water level
ExponentialFilter efRssi  = ExponentialFilter(FACTOR_RSSI);
ExponentialFilter efLight = ExponentialFilter(FACTOR_LIGHT);
ExponentialFilter efRain  = ExponentialFilter(FACTOR_RAIN);
ExponentialFilter efWater = ExponentialFilter(FACTOR_WATER);
SmoothSensorData smooth;

// Light level
const int LIGHT_VALUE_DARK     = 8;
const int LIGHT_VALUE_TWILIGHT = 32;
const int LIGHT_VALUE_CLOUDY   = 512;
const int LIGHT_VALUE_CLEAR    = 1024;
const int LIGHT_VALUE_MARGIN   = 2;        // Difference in value for status hysteresis
//
const unsigned char LIGHT_STATUS_DARK     = 1;
const unsigned char LIGHT_STATUS_TWILIGHT = 2;
const unsigned char LIGHT_STATUS_CLOUDY   = 3;
const unsigned char LIGHT_STATUS_CLEAR    = 4;
const unsigned char LIGHT_STATUS_SUNNY    = 5;

// Rain level
const int RAIN_VALUE_DRY    = 63;
const int RAIN_VALUE_DEW    = 127;
const int RAIN_VALUE_RAIN   = 512;
const int RAIN_VALUE_MARGIN = 4;         // Difference in value for status hysteresis
//
const unsigned char RAIN_STATUS_DRY    = 1;
const unsigned char RAIN_STATUS_DEW    = 2;
const unsigned char RAIN_STATUS_RAIN   = 3;
const unsigned char RAIN_STATUS_SHOWER = 4;

// Water level
const int SONAR_DISTANCE_MAX = 95;          // Maximal valid measured distance (to tank bottom)
const int WATER_TANK_EMPTY   = 89;          // Minimal water level (empty tank - to the bottom)
const int WATER_TANK_FULL    = 3;           // Maximal water level (full tank - to the brink)
const float WATER_TREND_MARGIN = 1.5;       // Difference in trend for status hysteresis
//
const unsigned char WATER_STATUS_STABLE  = 1; // No rain, no pumping
const unsigned char WATER_STATUS_FILLING = 2;
const unsigned char WATER_STATUS_PUMPING = 3;
//
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
    process();
}

void process() {
  measureRssi();
  measureLight();
  measureRain();
  measureWater();
#ifdef PHOTON_PUBLISH_VALUE
  publishParticle();
#endif
  publishThingspeak();
}

void measureRssi() {
  static unsigned long tsMeasure;
  if (millis() - tsMeasure >= PERIOD_MEASURE_RSSI || tsMeasure == 0) {
    tsMeasure = millis();
    rssiValue = efRssi.getValue(WiFi.RSSI());
    }
}

void measureLight() {
    static unsigned long tsMeasure, tsMeasureOld;
    static unsigned int cntMeasure;
    static int lightValueOld;
    static unsigned char lightStatusOld = lightStatus;
    if (millis() - tsMeasure >= PERIOD_MEASURE_LIGHT || tsMeasure == 0) {
        tsMeasure = millis();
        // Value
        while(smooth.registerData(analogRead(PIN_LIGHT)));
        lightValue = efLight.getValue(smooth.getMedian());
        // Initialize trend and statistics
        if (tsMeasureOld == 0 ) {
            tsMeasureOld = tsMeasure;
            lightValueOld = lightValue;
            // Calculate statistics
        } else if (tsMeasure > tsMeasureOld) {
            if (lightValueMin > lightValue) lightValueMin = lightValue;
            if (lightValueMax < lightValue) lightValueMax = lightValue;
            // Calculate trend
            if (++cntMeasure >= COUNT_TREND_LIGHT) {
                int lightDiff = abs(lightValue - lightValueOld);
                lightTrend = 6e4 * ((float)lightValue - (float)lightValueOld) / ((float)tsMeasure - (float)tsMeasureOld);
                cntMeasure = 0;
                tsMeasureOld = tsMeasure;
                lightValueOld = lightValue;
                // Determine status
                if (lightValue <= LIGHT_VALUE_DARK) {
                    lightStatus = LIGHT_STATUS_DARK;
                } else if (lightValue <= LIGHT_VALUE_TWILIGHT) {
                    lightStatus = LIGHT_STATUS_TWILIGHT;
                } else if (lightValue <= LIGHT_VALUE_CLOUDY) {
                    lightStatus = LIGHT_STATUS_CLOUDY;
                } else if (lightValue <= LIGHT_VALUE_CLEAR) {
                    lightStatus = LIGHT_STATUS_CLEAR;
                } else {
                    lightStatus = LIGHT_STATUS_SUNNY;
                }
                // Notify at status change
                if (lightStatusOld != lightStatus && lightDiff >= LIGHT_VALUE_MARGIN) {
#ifdef BLYNK_NOTIFY_LIGHT
                    String txtStatus;
                    switch (lightStatus) {
                        case LIGHT_STATUS_DARK:
                            txtStatus = String("Dark");
                            break;
                        case LIGHT_STATUS_TWILIGHT:
                            txtStatus = String("Twilight");
                            break;
                        case LIGHT_STATUS_CLOUDY:
                            txtStatus = String("Cloudy weather");
                            break;
                        case LIGHT_STATUS_SUNNY:
                            txtStatus = String("Sunny weather");
                            break;
                    }
                    Blynk.notify(BLYNK_LABEL + txtStatus);
#endif
                    lightStatusOld = lightStatus;
                }
            }
        }
    }
}

void measureRain() {
    static unsigned long tsMeasure, tsMeasureOld;
    static unsigned int cntMeasure;
    static int rainValueOld;
    static unsigned char rainStatusOld = rainStatus;
    if (millis() - tsMeasure >= PERIOD_MEASURE_RAIN || tsMeasure == 0) {
        tsMeasure = millis();
        // Value
        while(smooth.registerData(analogRead(PIN_RAIN)));
        rainValue = efRain.getValue(smooth.getMedian());
        // Initialize trend and statistics
        if (tsMeasureOld == 0 ) {
            tsMeasureOld = tsMeasure;
            rainValueOld = rainValue;
        // Calculate statistics
        } else if (tsMeasure > tsMeasureOld) {
            if (rainValueMin > rainValue) rainValueMin = rainValue;
            if (rainValueMax < rainValue) rainValueMax = rainValue;
            // Calculate trend
            if (++cntMeasure >= COUNT_TREND_RAIN) {
                int rainDiff = abs(rainValue - rainValueOld);
                rainTrend = 6e4 * ((float)rainValue - (float)rainValueOld) / ((float)tsMeasure - (float)tsMeasureOld);
                cntMeasure = 0;
                tsMeasureOld = tsMeasure;
                rainValueOld = rainValue;
                // Determine status
                if (rainValue <= RAIN_VALUE_DRY) {
                    rainStatus = RAIN_STATUS_DRY;
                } else if (rainValue <= RAIN_VALUE_DEW) {
                    rainStatus = RAIN_STATUS_DEW;
                } else if (rainValue <= RAIN_VALUE_RAIN) {
                    rainStatus = RAIN_STATUS_RAIN;
                } else {
                    rainStatus = RAIN_STATUS_SHOWER;
                }
                // Notify at status change
                if (rainStatusOld != rainStatus && rainDiff >= RAIN_VALUE_MARGIN) {
#ifdef BLYNK_NOTIFY_RAIN
                    String txtStatus;
                    switch (rainStatus) {
                        case RAIN_STATUS_DRY:
                          txtStatus = String("Dry whether");
                          break;
                        case RAIN_STATUS_DEW:
                          txtStatus = String("Moisture or dew");
                          break;
                        case RAIN_STATUS_RAIN:
                          txtStatus = String("Still rain");
                          break;
                        case RAIN_STATUS_SHOWER:
                          txtStatus = String("Heavy rain");
                          break;
                    }
                    Blynk.notify(BLYNK_LABEL + txtStatus);
#endif
                    rainStatusOld = rainStatus;
                }
            }
        }
    }
}


void measureWater() {
    static unsigned long tsMeasure, tsMeasureOld;
    static unsigned int cntMeasure;
    static int waterValueOld;
    static unsigned char waterStatusOld = waterStatus;
    if (millis() - tsMeasure >= PERIOD_MEASURE_WATER || tsMeasure == 0) {
        tsMeasure = millis();
        // Value
        while(smooth.registerData(sonar.getDistance()));
        waterValue = efWater.getValue(WATER_TANK_EMPTY - smooth.getMedian());
        // Initialize trend and statistics
        if (tsMeasureOld == 0 ) {
            tsMeasureOld = tsMeasure;
            waterValueOld = waterValue;
        // Calculate statistics
        } else if (tsMeasure > tsMeasureOld) {
            if (waterValueMin < 0)          waterValueMin = waterValueMax;
            if (waterValueMin > waterValue) waterValueMin = waterValue;
            if (waterValueMax < waterValue) waterValueMax = waterValue;
            // Calculate trend
            if (++cntMeasure >= COUNT_TREND_WATER) {
                waterTrend = 6e4 * ((float)waterValue - (float)waterValueOld) / ((float)tsMeasure - (float)tsMeasureOld);
                cntMeasure = 0;
                tsMeasureOld = tsMeasure;
                waterValueOld = waterValue;
                // Determine status
                if (fabs(waterTrend) <= WATER_TREND_MARGIN) waterStatus = WATER_STATUS_STABLE;
                if (waterTrend > WATER_TREND_MARGIN) waterStatus = WATER_STATUS_FILLING;
                if (waterTrend < -1 * WATER_TREND_MARGIN) waterStatus = WATER_STATUS_PUMPING;
                if (waterStatusOld != waterStatus) {
#ifdef BLYNK_NOTIFY_WATER
                    // Push notification
                    String txtStatus;
                    switch (waterStatus) {
                        case WATER_STATUS_STABLE:
                            txtStatus = String("stabilized");
                            break;
                        case WATER_STATUS_FILLING:
                            txtStatus = String("filled");
                            break;
                        case WATER_STATUS_PUMPING:
                            txtStatus = String("pumping out");
                            break;
                    }
                    Blynk.notify(BLYNK_LABEL + String("Water tank has been ") + txtStatus);
                    // Signal LED
                    switch (waterStatus) {
                        case WATER_STATUS_PUMPING:
                            ledWaterPump.on();
                            break;
                        default:
                            ledWaterPump.off();
                            break;
                    }
#endif
                    waterStatusOld = waterStatus;
                }
            }
        }
    }
}

void publishParticle() {
  static unsigned long tsPublish;
  if (millis() - tsPublish >= PERIOD_PUBLISH_PARTICLE || tsPublish == 0) {
    tsPublish = millis();
    Particle.publish("RSSI", String::format("%3d", rssiValue));
    Particle.publish("Light/Trend", String::format("%4d/%4.1f", lightValue, lightTrend));
    Particle.publish("Rain/Trend",  String::format("%4d/%4.1f", rainValue, rainTrend));
    Particle.publish("Water/Trend", String::format("%4d/%4.1f", waterValue, waterTrend));
  }
}

void publishThingspeak() {
  static unsigned long tsPublish;
  if (millis() - tsPublish >= PERIOD_PUBLISH_THINGSPEAK) {
    tsPublish = millis();
    bool isField = false;

#ifdef FIELD_RSSI_VALUE
    isField = true;
    ThingSpeak.setField(FIELD_RSSI_VALUE, rssiValue);
#endif

#ifdef FIELD_LIGHT_VALUE
    isField = true;
    ThingSpeak.setField(FIELD_LIGHT_VALUE, lightValue);
#endif

#ifdef FIELD_LIGHT_TREND
    isField = true;
    ThingSpeak.setField(FIELD_LIGHT_TREND, lightTrend);
#endif

#ifdef FIELD_RAIN_VALUE
    isField = true;
    ThingSpeak.setField(FIELD_RAIN_VALUE, rainValue);
#endif

#ifdef FIELD_RAIN_TREND
    isField = true;
    ThingSpeak.setField(FIELD_RAIN_TREND, rainTrend);
#endif

#ifdef FIELD_WATER_VALUE
    isField = true;
    ThingSpeak.setField(FIELD_WATER_VALUE, waterValue);
#endif

#ifdef FIELD_WATER_TREND
    isField = true;
    ThingSpeak.setField(FIELD_WATER_TREND, waterTrend);
#endif

    // Publish if there is something to
    if (isField) {
        int result = ThingSpeak.writeFields(CHANNEL_NUMBER, THINGSPEAK_TOKEN);
#ifdef PHOTON_PUBLISH_DEBUG
        Particle.publish("ThingSpeakResult", String(result));
#endif
    }
  }
}

// Frequency reading in Blynk mobile application
#ifdef VPIN_BOOT_VALUE
BLYNK_READ(VPIN_BOOT_VALUE)
{
    Blynk.virtualWrite(VPIN_BOOT_VALUE, bootCount);
}
#endif

#ifdef VPIN_RSSI_VALUE
BLYNK_READ(VPIN_RSSI_VALUE)
{
    Blynk.virtualWrite(VPIN_RSSI_VALUE, rssiValue);
}
#endif

#ifdef VPIN_LIGHT_VALUE
BLYNK_READ(VPIN_LIGHT_VALUE)
{
    Blynk.virtualWrite(VPIN_LIGHT_VALUE, lightValue);
}
#endif

#ifdef VPIN_LIGHT_TREND
BLYNK_READ(VPIN_LIGHT_TREND)
{
    Blynk.virtualWrite(VPIN_LIGHT_TREND, lightTrend);
}
#endif

#ifdef VPIN_LIGHT_MIN
BLYNK_READ(VPIN_LIGHT_MIN)
{
    Blynk.virtualWrite(VPIN_LIGHT_MIN, lightValueMin);
}
#endif

#ifdef VPIN_LIGHT_MAX
BLYNK_READ(VPIN_LIGHT_MAX)
{
    Blynk.virtualWrite(VPIN_LIGHT_MAX, lightValueMax);
}
#endif

#ifdef VPIN_RAIN_VALUE
BLYNK_READ(VPIN_RAIN_VALUE)
{
    Blynk.virtualWrite(VPIN_RAIN_VALUE, rainValue);
}
#endif

#ifdef VPIN_RAIN_TREND
BLYNK_READ(VPIN_RAIN_TREND)
{
    Blynk.virtualWrite(VPIN_RAIN_TREND, rainTrend);
}
#endif

#ifdef VPIN_RAIN_MIN
BLYNK_READ(VPIN_RAIN_MIN)
{
    Blynk.virtualWrite(VPIN_RAIN_MIN, rainValueMin);
}
#endif

#ifdef VPIN_RAIN_MAX
BLYNK_READ(VPIN_RAIN_MAX)
{
    Blynk.virtualWrite(VPIN_RAIN_MAX, rainValueMax);
}
#endif

#ifdef VPIN_WATER_VALUE
BLYNK_READ(VPIN_WATER_VALUE)
{
    Blynk.virtualWrite(VPIN_WATER_VALUE, waterValue);
}
#endif

#ifdef VPIN_WATER_TREND
BLYNK_READ(VPIN_WATER_TREND)
{
    Blynk.virtualWrite(VPIN_WATER_TREND, waterTrend);
}
#endif

#ifdef VPIN_WATER_MIN
BLYNK_READ(VPIN_WATER_MIN)
{
    Blynk.virtualWrite(VPIN_WATER_MIN, waterValueMin);
}
#endif

#ifdef VPIN_WATER_MAX
BLYNK_READ(VPIN_WATER_MAX)
{
    Blynk.virtualWrite(VPIN_WATER_MAX, waterValueMax);
}
#endif
