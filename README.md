# Arduino Water Tank Monitor

Arduino Uno water tank monitor with:

- HanRun W5100 Ethernet Shield
- ALS-MPM-2F hydrostatic pressure sensor
- 4–20 mA current-loop input
- 16×2 I²C LCD
- JSON web endpoint for Loxone or other systems

## Features

- Static Ethernet configuration
- JSON output over HTTP
- Water level in percent
- Water height in centimeters and meters
- Estimated tank volume in liters
- Sensor voltage and current monitoring
- Calibration status
- Averaging of 50 ADC measurements
- LCD refresh once every 60 seconds
- SRAM-saving `F()` macros for fixed text

## Hardware

- Arduino Uno
- W5100 Ethernet Shield
- ALS-MPM-2F pressure sensor
- 220 Ω resistor
- 16×2 I²C LCD
- 24 V DC power supply for the sensor
- USB power supply for the Arduino

## Sensor wiring

The pressure sensor uses a 4–20 mA output.

```text
24 V positive       -> Sensor red wire
Sensor black        -> Arduino A0
Sensor black        -> One side of 220 Ω resistor
Other resistor side -> 24 V supply 0 V
Arduino GND         -> 24 V supply 0 V
```

The Arduino ground and the 24 V supply ground must be connected.

The 220 Ω resistor converts the sensor current into a measurable voltage:

```text
4 mA  × 220 Ω = 0.88 V
20 mA × 220 Ω = 4.40 V
```

Do not connect 24 V directly to an Arduino input.

## LCD wiring

```text
LCD GND -> Arduino GND
LCD VCC -> Arduino 5V
LCD SDA -> Arduino A4
LCD SCL -> Arduino A5
```

The default I²C address is:

```cpp
const byte lcdAddress = 0x27;
```

Some displays use `0x3F`.

## Required Arduino libraries

```cpp
#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
```

`SPI`, `Ethernet`, and `Wire` are normally included with the Arduino IDE.
Install a compatible `LiquidCrystal_I2C` library through the Library Manager.

## Network settings

Default settings:

```cpp
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

IPAddress ip(172, 16, 42, 66);
EthernetServer server(80);
```

The JSON endpoint is available at:

```text
http://172.16.42.66/
```

Change the IP address when necessary to match the local network.

## ADC measurement averaging

Every stored sensor value is the average of 50 ADC readings:

```cpp
const int numberOfMeasurements = 50;
const int measurementDelayMs = 5;
```

The first ADC reading is discarded. The next 50 readings are added together and divided by 50.

The resulting average is stored in:

```cpp
currentAdcValue
```

The same averaged value is used for:

- JSON output
- LCD output
- Serial Monitor output
- Percentage calculation
- Height calculation
- Liter calculation

An HTTP request does not trigger a separate raw ADC reading. It returns the most recently stored 50-reading average.

## Measurement interval

The sensor is measured periodically:

```cpp
const unsigned long sensorMeasurementIntervalMs = 1000UL;
```

With this setting, a new 50-reading average is created approximately once per second.

For a slower update, for example once every five seconds:

```cpp
const unsigned long sensorMeasurementIntervalMs = 5000UL;
```

## LCD behavior

The LCD uses one fixed screen.

Example:

```text
42.5% 4250L
Height: 106cm
```

Line 1 shows the water level percentage and available liters.

Line 2 shows the water height in centimeters without decimal digits.

The LCD is refreshed once per minute:

```cpp
const unsigned long lcdRefreshIntervalMs = 60000UL;
```

The slow LCD refresh reduces visible changes and flickering. The sensor and JSON values can still update more frequently than the LCD.

## Calibration

Default calibration values:

```cpp
const int adcEmpty = 196;
const int adcFull = 820;
const float tankHeightCm = 250.0;
const float tankCapacityLiters = 10000.0;
```

Replace `adcEmpty` with the averaged ADC value measured when the tank is empty.

Replace `adcFull` with the averaged ADC value measured when the tank is full.

The calibration is valid only when:

```text
adcFull > adcEmpty
```

## Percentage calculation

```text
(adcValue - adcEmpty)
--------------------- × 100
(adcFull - adcEmpty)
```

Values below the empty calibration point are limited to `0%`.
Values above the full calibration point are limited to `100%`.

## Height calculation

```text
waterHeightCm = tankHeightCm × percentage / 100
```

The JSON output contains both centimeters and meters.

## Liter calculation

```text
liters = tankCapacityLiters × percentage / 100
```

This linear calculation is correct only when tank volume increases linearly with water height, such as in a rectangular tank or a vertical tank with straight sides.

For a horizontal cylindrical tank or an irregular tank, a nonlinear formula or lookup table is required.

## Sensor status

The sensor is considered active when the calculated current is approximately between:

```text
3.5 mA and 22.0 mA
```

A value outside this range can indicate:

- Broken sensor wiring
- Missing 24 V supply
- Incorrect grounding
- Sensor fault

## JSON example

```json
{
  "device": {
    "name": "water_tank",
    "version": "1.4",
    "uptime_seconds": 12345
  },
  "calibration": {
    "adc_empty": 196,
    "adc_full": 820,
    "valid": true
  },
  "sensor": {
    "adc": 285,
    "voltage": 1.393,
    "current_mA": 6.332,
    "active": true
  },
  "water": {
    "percentage": 14.3,
    "height_cm": 35.7,
    "height_m": 0.357,
    "liters": 1426.3
  }
}
```

The values above are examples.

## Loxone integration

Loxone can periodically request:

```text
http://172.16.42.66/
```

Useful JSON paths include:

```text
water.percentage
water.height_cm
water.height_m
water.liters
sensor.adc
sensor.voltage
sensor.current_mA
sensor.active
calibration.valid
```

## Serial Monitor

Use:

```text
9600 baud
```

The Serial Monitor shows values after a JSON request, including ADC, voltage, current, percentage, height, and volume.

## Stability notes

A small ADC variation is normal. For example, a reading between `284` and `287` is a variation of only three ADC counts.

The 50-reading average helps reduce random noise.

For additional stability:

- Use a common ground
- Keep analog wiring short
- Keep sensor wiring away from Ethernet and power cables
- Use a stable 5 V Arduino supply
- Measure the real Arduino 5 V voltage and update `arduinoReferenceVoltage`
- Avoid unnecessary LCD clearing
- Keep fixed strings inside `F()`

## Safety

- Never connect 24 V directly to Arduino A0
- Verify the resistor value before powering the circuit
- Confirm common ground before testing
- Disconnect power before changing wiring
- Check the sensor datasheet for wire colors and polarity
