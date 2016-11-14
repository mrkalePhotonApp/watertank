# watertank
Application for observing water tank, rain intensity, and ambient light intensity as well as boot count and RSSI.

- The application reads from various sensors and publishes gained values in order to observe the cottage garden.


- *Physical observation*:
  - The ambient light intensity in bits.
  - The rain intensity in bits.
  - The water level hight in centimeters in a water tank.
  - For each sensor the trend is calculated in units change per minute.
  - For each sensor the status is determined based on either the trend calculation or value buckets.
  - For each sensor the long term minimal and maximal value in units is calculated.


- *System observation*:
  - Number of boots since recent power-up.
  - RSSI wifi signal intensity.


- *Hardware watchdog timers* built in microcontroller are utilized for reseting the microcontroller when it freezes in order to ensure unattended operation.


- Publishing to cloud services:
  - **ThingSpeak** for graphing and further analyzing of smoothed and filtered values.
  - **Blynk** for mobile application control and observation.


- The application utilizes a separate include **credentials** file with credentials to cloud services.
  - The credential file contains just placeholder for credentials.
  - Replace credential placeholders with real ones only in the Particle dashboard in order not to share or reveal them.


## Light measurement
The ambient light intensity is measured as a non-negative integer by analog sensor **TEMT6000** in bits within range 0 ~ 4095 with 12-bit resolution. The application calculates following values.

- **Current light intensity**. The value is measured several times in a measurement burst and statistically smoothed with the library *SmoothSensorData* in order to stabilize analog-digital converter. Then the smoothed valued is exponentially filtered by the library *ExponentialFilter* in order to smooth steep changes in measurements.

- **Minute light trend**. It is an averaged light intensity change speed in bits pre minute. From two subsequent measurements and their real time period the instant trend is calculated as a ratio of intensity difference and time stamp difference and extrapolated to a minute.

- **Minimal and maximal intensity**. Those are long term statistics stored in backup memory, so that they are retained across booting the microcontroller.

- **Relative intensity**. It is an percentage expression (rounded to integers) of the current intensity value on current statistical range, i.e, difference between maximal and minimal intensity. So, the value equal to current minimal value is expressed as 0% and the value equal to current maximal value is expressed as 100%.


## ThingSpeak
For graphing and eventually further processing only *current light intensity* and *minute light trend* is sent to the ThingSpeak cloud.


### Blynk
For Blynk cloud and composing a mobile application following aspects are provided in order to observe and control the light measurement.

- For **Blynk push notifications** the expected range of light values is divided into several buckets by respective intensity value thresholds. Each bucket is marked by particular light intensity status and for each status a corresponding notification featured text is defined. A push notification is generated and sent to a mobile application only when a light status is changed.

- All measured values a provided to a mobile application only on demand with help related **Blynk methods and virtual pins**. Values are not pushed to the Blynk cloud, just a mobile application reads them at its time intervals.

- The application provides a Blynk method for **resetting statistical extremes**, i.e., minimal and maximal intensity. It is invoked by a mobile application Blynk button press at sending a logical HIGH to the microcontroller. Is is useful when we want to start a new statistical observation, because the backup memory is cleared at power down the microcontroller only.


## Rain measurement
The rain intensity is measured as a non-negative integer by resistive sensor board **YL-83** in bits within range 0 ~ 4095 with 12-bit resolution. The board is utilized with additional 1 MOhm resistor in a voltage divider connected to 5V rail of the microcontroller. This configuration provides the voltage range to approx. 3.1V at totally dipped board in the water, so that it is suitable for direct analog measuring. The application calculates following values.

- **Current rain intensity**. The value is measured several times in a measurement burst and statistically smoothed with the library *SmoothSensorData* in order to stabilize analog-digital converter. Then the smoothed valued is exponentially filtered by the library *ExponentialFilter* in order to smooth steep changes in measurements.

- **Minute rain trend**. It is an averaged rain intensity change speed in bits pre minute. From two subsequent measurements and their real time period the instant trend is calculated as a ratio of intensity difference and time stamp difference and extrapolated to a minute.

- **Minimal and maximal intensity**. Those are long term statistics stored in backup memory, so that they are retained across booting the microcontroller.

- **Relative intensity**. It is an percentage expression (rounded to integers) of the current intensity value on current statistical range, i.e, difference between maximal and minimal intensity. So, the value equal to current minimal value is expressed as 0% and the value equal to current maximal value is expressed as 100%.


## ThingSpeak
For graphing and eventually further processing only *current rain intensity* and *minute rain trend* is sent to the ThingSpeak cloud.


### Blynk
For Blynk cloud and composing a mobile application following aspects are provided in order to observe and control the rain measurement.

- For **Blynk push notifications** the minute water expected range of rain values is divided into several buckets by respective intensity value thresholds. Each bucket is marked by particular rain intensity status and for each status a corresponding notification featured text is defined. A push notification is generated and sent to a mobile application only when a rain status is changed.

- All measured values a provided to a mobile application only on demand with help related **Blynk methods and virtual pins**. Values are not pushed to the Blynk cloud, just a mobile application reads them at its time intervals.


## Water level measurement
The water level hight in a water tank is measured as a non-negative integer by ultrasonic sersor **HC-SR04** in centimeters within the range 2 ~ 95 cm with resolution 1 cm. The application calculates following values.

- **Current water level**. The value is measured several times in a measurement burst and statistically smoothed with the library *SmoothSensorData* in order to get rid of ultrasonic sensor drifts. Then the smoothed valued is exponentially filtered by the library *ExponentialFilter* in order to smooth steep changes in measurements.

- **Minute water level trend**. It is an averaged water level change speed in centimeters pre minute. From two subsequent measurements and their real time period the instant trend is calculated as a ratio of level difference and time stamp difference and extrapolated to a minute.

- **Minimal and maximal level**. Those are long term statistics stored in backup memory, so that they are retained across booting the microcontroller.


## ThingSpeak
For graphing and eventually further processing only *current water level hight* and minute *water level trend* is sent to the ThingSpeak cloud.


### Blynk
For Blynk cloud and composing a mobile application following aspects are provided in order to observe and control the water level measurement.

- For **Blynk push notifications** the minute water level trend is relevant, not the current level hight and following statuses are distinquished and notified:

  - **Water level stable**. This status is determined at zero trend or when its absolute value is withing a threshold in order not to generated many push notification at small level drifts. This status indicates no rain as well.
  
  - **Water tank is filling**. This status is determined at positive trend value greater then a threshold. It means, that the water level is increasing. At the same time this status indicates, that it has been raining. 
  
  - **Water pump is working**. This status is determined at negative trend value less then the negative value of a threshold. It means, that the water level is decreasing, because a water pump is working. At the same time this status indicates long lasting or heavy rain and a water pump has been switched on in order to prevent the water tank to be overfull and water is overflowing over the tank brink.
  
  For each status a corresponding notification featured text is defined. A push notification is generated and sent to a mobile application only when a water level status is changed.

- All measured values a provided to a mobile application only on demand with help related **Blynk methods and virtual pins**. Values are not pushed to the Blynk cloud, just a mobile application reads them at its time intervals.

- The application switches on and off **Blynk LEDs** for aforementioned dynamic statuss, filling and pumping, for better and smarter indication in a mobile application.
