# watertank
Application for observing water tank, rain intensity, and ambient light intensity as well as boot count and RSSI.

- The application reads from various sensors and publishes gained values in order to observe the cottage garden.

- Observation sensors:
  - Water level in water tank by ultrasonic sersor **HC-SR04** in centimeters within range 2 ~ 90 cm with resolution 1 cm.
  - Rain intensity by analog resistive sensor board **YL-83** in bits within range 0 ~ 4095 with 12-bit resolution.
  - Ambient light intensity by analog sensor **TEMT6000** in bits within range 0 ~ 4095 with 12-bit resolution.

- System observation:
  - Number of boots since recent power-up.
  - RSSI wifi signal intensity.

- All measured values are statistically processed:
  - A batch (burst) of measured values is statistically smoothed by median and result is considered as a measured value.
  - Smoothed (measured) values is then processed by exponential filter with individual smoothing factor and result is considered as a final processed value.

- *Hardware watchdog timers* built in microcontroller are utilized for reseting the microcontroller when it freezes in order to ensure unattended operation.

- Publishing to cloud services:
  - *ThingSpeak* for graphing and further analyzing of smoothed and filtered values.
  - *Blynk* for publishing smoothed and filtered values in mobile application.

- The application utilizes a separate include **credentials** file with credentials to cloud services.
  - The credential file contains just placeholder for credentials.
  - Replace credential placeholders with real ones only in the Particle dashboard in order not to share or reveal them.

