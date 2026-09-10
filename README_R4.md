# Water Tank Monitor — Arduino UNO R4 WiFi

A Wi-Fi connected water tank monitor built with an **Arduino UNO R4 WiFi**, a **4–20 mA hydrostatic level sensor**, an **I²C 16×2 LCD**, and a **momentary push button**.

The project measures tank level, calculates percentage, water height and volume, exposes the data as JSON over HTTP, and can be integrated with systems such as **Loxone**.

The LCD normally stays off. A short button press turns it on for 5 minutes. Holding the button for 5 seconds shows Wi-Fi diagnostics such as connection status, SSID and IP address.

---

## Features

- 4–20 mA hydrostatic level sensor input
- 100 ADC samples averaged per measurement
- New sensor measurement every 5 seconds
- Wi-Fi connection using the UNO R4 WiFi
- HTTP JSON endpoint on port 80
- Water level as percentage, height and liters
- Sensor voltage and loop current diagnostics
- LCD display with tank status
- LCD backlight normally off
- Short button press: LCD turns on for 5 minutes
- Long button press for 5 seconds: shows Wi-Fi status, SSID and IP address
- Automatic Wi-Fi reconnect
- Suitable for Loxone Virtual HTTP Input integration

---

## Parts List

### Main Components

| Quantity | Part | Notes |
|---|---|---|
| 1 | Arduino UNO R4 WiFi | Main controller with built-in Wi-Fi |
| 1 | 4–20 mA hydrostatic level sensor | Example: 0–5 m water level sensor |
| 1 | 24 V DC power supply | Powers the 4–20 mA sensor |
| 1 | 220 Ω resistor | Converts 4–20 mA loop current to a measurable voltage |
| 1 | 16×2 I²C LCD | Common address is `0x27`, sometimes `0x3F` |
| 1 | 16 mm metal waterproof momentary push button | Normally Open (NO), momentary/self-reset |
| 1 | USB-C cable | For programming and/or powering the UNO R4 |

---

## Wiring

### 4–20 mA Sensor

The sensor is powered by the external 24 V supply.

```text
24V +  ───────────── Sensor +
                       |
                       |
Sensor - ──────────────+──── Arduino A0
                       |
                     220 Ω
                       |
24V 0V ────────────────+──── Arduino GND
```

Important:

- Arduino GND and the 24 V supply 0 V must share a common ground.
- Do **not** connect 24 V to the Arduino 5 V pin.
- The 220 Ω resistor converts the sensor current into voltage.

With a 220 Ω resistor:

```text
4 mA  × 220 Ω = 0.88 V
20 mA × 220 Ω = 4.40 V
```

This keeps the signal within the Arduino analog input range.

### LCD Wiring

```text
LCD GND → Arduino GND
LCD VCC → Arduino 5V
LCD SDA → Arduino SDA
LCD SCL → Arduino SCL
```

The sketch uses:

```cpp
LiquidCrystal_I2C lcd(0x27, 16, 2);
```

If your LCD does not respond, try address `0x3F`.

### Push Button Wiring

Use a **momentary Normally Open (NO)** button.

```text
Arduino pin 2 ───── Button ───── GND
```

The sketch uses the Arduino internal pull-up resistor:

```cpp
pinMode(lcdButtonPin, INPUT_PULLUP);
```

No external resistor is required.

Button behavior:

- Short press: LCD turns on for 5 minutes
- Hold for 5 seconds: Wi-Fi diagnostics are shown for 20 seconds
- Pressing the button again restarts the 5-minute LCD timer

---

## Required Arduino Libraries

Install these libraries in the Arduino IDE:

- `WiFiS3`
- `Wire`
- `LiquidCrystal_I2C`

`WiFiS3` is used by the Arduino UNO R4 WiFi.

Depending on the installed `LiquidCrystal_I2C` library, the Arduino IDE may show a warning such as:

```text
library LiquidCrystal I2C claims to run on avr architecture(s)
```

This is only a compatibility warning if the sketch still compiles. If the LCD does not work on the UNO R4, install a version of the library that supports the `renesas_uno` architecture.

---

## Wi-Fi Configuration

Edit these lines in the sketch:

```cpp
const char* wifiSsid = "YOUR_WIFI_NAME";
const char* wifiPassword = "YOUR_WIFI_PASSWORD";
```

Example:

```cpp
const char* wifiSsid = "HomeWiFi";
const char* wifiPassword = "MyPassword";
```

After uploading the sketch, open the Serial Monitor at `115200 baud`.

You should see output similar to:

```text
Connecting to WiFi: HomeWiFi
....
WiFi connected.
SSID: HomeWiFi
IP address: 172.16.42.81
Signal strength: -52 dBm

Open in browser: http://172.16.42.81
HTTP server started.
```

For reliable home automation integration, it is recommended to create a **DHCP reservation** in your router so the Arduino always receives the same IP address.

---

## Wi-Fi Diagnostics on the LCD

Hold the button for 5 seconds.

The LCD alternates between pages such as:

```text
WiFi: CONNECTED
SSID:HomeWiFi
```

and:

```text
IP address:
172.16.42.81
```

If Wi-Fi is unavailable:

```text
WiFi: OFFLINE
SSID:HomeWiFi
```

followed by:

```text
No connection
No IP address
```

The Wi-Fi password is intentionally never shown on the LCD.

---

## Measurement Configuration

The important settings are:

```cpp
const int numberOfMeasurements = 100;
const unsigned long sensorMeasurementIntervalMs = 5000UL;
```

This means:

- 100 ADC samples are averaged for every measurement
- A new averaged sensor value is calculated every 5 seconds

The LCD refresh interval is:

```cpp
const unsigned long lcdRefreshIntervalMs = 60000UL;
```

The LCD therefore refreshes once per minute while it is active.

---

## ADC Resolution

The UNO R4 supports a higher ADC resolution, but this project currently uses:

```cpp
analogReadResolution(10);
```

This keeps the ADC scale compatible with the previous Arduino Uno calibration:

```text
0 ... 1023
```

The UNO R4 can later be changed to a higher ADC resolution, but the tank must then be recalibrated.

---

## Reference Voltage

The sketch contains:

```cpp
const float arduinoReferenceVoltage = 5.00;
```

Measure the actual voltage between the UNO R4 **5V** and **GND** pins with a multimeter and update this value if necessary.

Example:

```cpp
const float arduinoReferenceVoltage = 4.93;
```

This improves the calculated sensor voltage and loop current.

---

## Calibration

The current calibration values are:

```cpp
const int adcEmpty = 196;
const int adcFull = 820;
```

These values define:

```text
adcEmpty → 0%
adcFull  → 100%
```

Because ADC behavior can differ between boards, recalibrate after moving from an older Arduino Uno to the UNO R4 WiFi.

### Empty Calibration

With the tank empty, observe the averaged ADC value in the Serial Monitor and use it as:

```cpp
const int adcEmpty = VALUE;
```

### Full Calibration

With the tank at the desired full level, record the averaged ADC value and use it as:

```cpp
const int adcFull = VALUE;
```

---

## Tank Configuration

The example sketch uses:

```cpp
const float tankHeightCm = 250.0;
const float tankCapacityLiters = 10000.0;
```

Change these values to match your tank.

The current sketch assumes a linear relationship between water height and volume. This is correct for tanks with a constant cross-sectional area. For irregular or horizontal cylindrical tanks, use a different volume formula.

---

## LCD Display

During normal operation, the LCD shows:

```text
13% 1298L
Height: 33cm
```

The LCD backlight is normally off. A short button press turns it on for 5 minutes.

---

## HTTP JSON API

Open the Arduino IP address in a browser:

```text
http://ARDUINO_IP/
```

Example:

```text
http://172.16.42.81/
```

The Arduino returns JSON similar to:

```json
{
  "device": {
    "name": "water_tank",
    "version": "2.1-r4wifi",
    "uptime_seconds": 1085
  },
  "calibration": {
    "adc_empty": 196,
    "adc_full": 820,
    "valid": true
  },
  "sensor": {
    "adc": 281.0,
    "voltage": 1.264,
    "current_mA": 5.743,
    "active": true
  },
  "wifi": {
    "connected": true,
    "rssi": -52
  },
  "water": {
    "percentage": 14,
    "height_cm": 34,
    "height_m": 0.341,
    "liters": 1362
  }
}
```

The JSON request does **not** trigger a new ADC measurement. It always returns the latest averaged sensor measurement, which is updated every 5 seconds.

---

## JSON Fields

### Device

| Field | Description |
|---|---|
| `device.name` | Device name |
| `device.version` | Firmware version |
| `device.uptime_seconds` | Arduino uptime |

### Calibration

| Field | Description |
|---|---|
| `calibration.adc_empty` | ADC value representing empty |
| `calibration.adc_full` | ADC value representing full |
| `calibration.valid` | Whether calibration range is valid |

### Sensor

| Field | Description |
|---|---|
| `sensor.adc` | Averaged ADC value |
| `sensor.voltage` | Calculated sensor voltage |
| `sensor.current_mA` | Calculated 4–20 mA loop current |
| `sensor.active` | Basic sensor-active check |

### Wi-Fi

| Field | Description |
|---|---|
| `wifi.connected` | Wi-Fi connection status |
| `wifi.rssi` | Wi-Fi signal strength in dBm |

### Water

| Field | Description |
|---|---|
| `water.percentage` | Tank level percentage |
| `water.height_cm` | Water height in cm |
| `water.height_m` | Water height in m |
| `water.liters` | Calculated tank volume |

---

## Loxone Integration

Create a **Virtual HTTP Input** in Loxone Config.

Example URL:

```text
http://172.16.42.81
```

A polling cycle of 10–30 seconds is suitable for a water tank.

### Percentage

Create a Virtual HTTP Input Command with:

```text
"percentage": \v
```

Recommended settings:

```text
Unit: %
Minimum: 0
Maximum: 100
```

### Liters

```text
"liters": \v
```

Recommended settings:

```text
Unit: L
Minimum: 0
Maximum: tank capacity
```

### Height

```text
"height_cm": \v
```

Recommended settings:

```text
Unit: cm
Minimum: 0
Maximum: tank height
```

### Suggested Tank Statuses

A Loxone Status function block can be used to create human-readable tank states:

```text
0–10%    → Almost Empty
10–30%   → Low
30–80%   → Normal
80–100%  → Full
```

---

## Serial Monitor

The Arduino outputs diagnostics such as:

```text
ADC: 281.2 | Voltage: 1.374 V | Current: 6.246 mA | Water: 13.7 % | Height: 34.2 cm | Volume: 1368 L
```

This is useful for calibration, wiring checks, sensor diagnostics and Wi-Fi troubleshooting.

---

## Recommended Installation Notes

- Use screw terminals instead of loose jumper wires
- Keep the 24 V sensor wiring separate from USB and I²C wiring
- Use a suitable enclosure
- Add strain relief to sensor cables
- Add a small fuse to the 24 V supply
- Keep the 220 Ω resistor connection close to the Arduino analog input
- Optionally place a 100 nF capacitor between A0 and GND for additional filtering
- Use a router DHCP reservation for the Arduino
- Do not expose the HTTP endpoint directly to the public internet

---

## Safety

The sensor side uses 24 V DC.

Before powering the circuit:

1. Verify the 24 V supply polarity with a multimeter
2. Confirm Arduino GND is connected to 24 V 0 V
3. Confirm 24 V is **not** connected to Arduino 5 V
4. Confirm the sensor signal at A0 cannot exceed the Arduino input range
5. Confirm the 220 Ω resistor is correctly connected

---

## Future Improvements

- Use the higher-resolution UNO R4 ADC
- Add an ADS1115 external ADC
- Add a configuration webpage
- Store calibration values in flash memory
- Add a `/percentage` endpoint for simplified integrations
- Add MQTT support
- Add OTA firmware updates
- Add historical logging
- Add sensor fault alarms
- Add a custom web dashboard
- Add Home Assistant integration

---

## License

This project can be released under the MIT License or another license of your choice.

If you publish it publicly, add a `LICENSE` file to the repository.

---

## Suggested Repository Structure

```text
water-tank-monitor/
├── README.md
├── water_tank_monitor.ino
├── LICENSE
└── docs/
    └── wiring-diagram.png
```

---

## Project Goal

This project is designed as a simple, stable and readable residential water-tank monitor with local Wi-Fi access, Loxone integration and easy on-device diagnostics.
