#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =====================================================
// NETWORK SETTINGS
// =====================================================

byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

IPAddress ip(172, 16, 42, 66);

EthernetServer server(80);

// =====================================================
// LCD SETTINGS
// =====================================================

// Common I2C LCD addresses are 0x27 and 0x3F.
const byte lcdAddress = 0x27;
const byte lcdColumns = 16;
const byte lcdRows = 2;

LiquidCrystal_I2C lcd(lcdAddress, lcdColumns, lcdRows);

// Refresh the LCD once every minute.
const unsigned long lcdRefreshIntervalMs = 60000UL;

// =====================================================
// SENSOR SETTINGS
// =====================================================

const int sensorPin = A0;

// Resistor used to convert the 4-20 mA signal to voltage.
const float resistorOhm = 220.0;

// Arduino analog reference voltage.
//
// This value can later be replaced with the voltage
// measured between Arduino 5V and Arduino GND.
const float arduinoReferenceVoltage = 5.0;

// =====================================================
// CALIBRATION
// =====================================================

// ADC value measured when the tank is empty.
const int adcEmpty = 196;

// ADC value measured when the tank is full.
//
// Replace this example value with the real measured value.
const int adcFull = 820;

// Actual water height when the tank is completely full.
const float tankHeightCm = 250.0;

// Total tank capacity.
//
// This calculation assumes that the volume increases
// linearly with the water height.
const float tankCapacityLiters = 10000.0;

// =====================================================
// MEASUREMENT SETTINGS
// =====================================================

// Number of ADC measurements used for averaging.
const int numberOfMeasurements = 50;

// Delay between individual ADC measurements.
const int measurementDelayMs = 5;

// Perform a new sensor measurement every second.
const unsigned long sensorMeasurementIntervalMs = 1000UL;

// =====================================================
// CURRENT STATE
// =====================================================

// Only the current ADC value is stored permanently.
// Other values are calculated when required.
int currentAdcValue = 0;

unsigned long previousSensorMeasurementTime = 0;
unsigned long previousLcdRefreshTime = 0;

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(9600);

  Serial.println();
  Serial.println(F("Water tank monitor starting..."));

  initializeLcd();
  initializeEthernet();

  // Perform the first measurement immediately.
  currentAdcValue = readAverageAdc();

  // Show the first values immediately.
  updateLcdDisplay();

  previousSensorMeasurementTime = millis();
  previousLcdRefreshTime = millis();
}

// =====================================================
// MAIN PROGRAM
// =====================================================

void loop() {
  updateSensorAtInterval();
  updateLcdAtInterval();
  handleEthernetClient();
}

// =====================================================
// LCD INITIALIZATION
// =====================================================

void initializeLcd() {
  Wire.begin();

  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Water tank"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));

  delay(1000);

  clearLcdLine(0);
  clearLcdLine(1);
}

// =====================================================
// ETHERNET INITIALIZATION
// =====================================================

void initializeEthernet() {
  Ethernet.begin(mac, ip);

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println(F("ERROR: Ethernet shield not found."));

    clearLcdLine(0);
    lcd.setCursor(0, 0);
    lcd.print(F("Ethernet error"));

    clearLcdLine(1);
    lcd.setCursor(0, 1);
    lcd.print(F("Shield missing"));

    while (true) {
      delay(1);
    }
  }

  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println(
      F("WARNING: Ethernet cable not connected.")
    );
  }

  server.begin();

  Serial.print(F("Web server active at: http://"));
  Serial.println(Ethernet.localIP());
}

// =====================================================
// PERIODIC SENSOR UPDATE
// =====================================================

void updateSensorAtInterval() {
  unsigned long currentTime = millis();

  if (
    currentTime - previousSensorMeasurementTime >=
    sensorMeasurementIntervalMs
  ) {
    previousSensorMeasurementTime = currentTime;

    currentAdcValue = readAverageAdc();
  }
}

// =====================================================
// PERIODIC LCD UPDATE
// =====================================================

void updateLcdAtInterval() {
  unsigned long currentTime = millis();

  if (
    currentTime - previousLcdRefreshTime >=
    lcdRefreshIntervalMs
  ) {
    previousLcdRefreshTime = currentTime;

    updateLcdDisplay();
  }
}

// =====================================================
// LCD DISPLAY
// =====================================================

void updateLcdDisplay() {
  float percentage =
    calculatePercentage(currentAdcValue);

  float liters =
    calculateLiters(percentage);

  float waterHeightCm =
    calculateWaterHeightCm(percentage);

  float voltage =
    calculateVoltage(currentAdcValue);

  float currentMilliampere =
    calculateCurrentMilliampere(voltage);

  bool calibrationValid =
    adcFull > adcEmpty;

  bool sensorActive =
    currentMilliampere >= 3.5 &&
    currentMilliampere <= 22.0;

  // Clear and rewrite the complete first line.
  clearLcdLine(0);
  lcd.setCursor(0, 0);

  if (calibrationValid && sensorActive) {
    // Example: "42.5%  4250L"
    lcd.print(percentage, 1);
    lcd.print(F("% "));

    lcd.print(liters, 0);
    lcd.print(F("L"));
  } else {
    lcd.print(F("Sensor fault"));
  }

  // Clear and rewrite the complete second line.
  clearLcdLine(1);
  lcd.setCursor(0, 1);

  if (calibrationValid && sensorActive) {
    // Height is shown without decimal places.
    // Example: "Height: 106cm"
    lcd.print(F("Height: "));
    lcd.print(waterHeightCm, 0);
    lcd.print(F("cm"));
  } else if (!calibrationValid) {
    lcd.print(F("Calibration err"));
  } else {
    lcd.print(F("Height: ---cm"));
  }
}

// =====================================================
// CLEAR ONE LCD LINE
// =====================================================

void clearLcdLine(byte row) {
  lcd.setCursor(0, row);
  lcd.print(F("                "));
}

// =====================================================
// ETHERNET CLIENT HANDLING
// =====================================================

void handleEthernetClient() {
  EthernetClient client = server.available();

  if (!client) {
    return;
  }

  Serial.println(F("New client connected."));

  bool currentLineIsBlank = true;
  unsigned long startTime = millis();

  while (client.connected()) {

    // Stop when the client does not complete the request
    // within two seconds.
    if (millis() - startTime > 2000UL) {
      Serial.println(F("Client timeout."));
      break;
    }

    if (client.available()) {
      char character = client.read();

      // An empty line indicates the end of the HTTP request.
      if (
        character == '\n' &&
        currentLineIsBlank
      ) {
        sendJsonResponse(client);
        break;
      }

      if (character == '\n') {
        currentLineIsBlank = true;
      } else if (character != '\r') {
        currentLineIsBlank = false;
      }
    }
  }

  delay(1);
  client.stop();

  Serial.println(F("Client disconnected."));
}

// =====================================================
// JSON RESPONSE
// =====================================================

void sendJsonResponse(EthernetClient &client) {
  int adcValue = currentAdcValue;

  float voltage =
    calculateVoltage(adcValue);

  float currentMilliampere =
    calculateCurrentMilliampere(voltage);

  float percentage =
    calculatePercentage(adcValue);

  float waterHeightCm =
    calculateWaterHeightCm(percentage);

  float waterHeightMeter =
    waterHeightCm / 100.0;

  float liters =
    calculateLiters(percentage);

  bool sensorActive =
    currentMilliampere >= 3.5 &&
    currentMilliampere <= 22.0;

  bool calibrationValid =
    adcFull > adcEmpty;

  // HTTP header
  client.println(F("HTTP/1.1 200 OK"));
  client.println(
    F("Content-Type: application/json; charset=utf-8")
  );
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Cache-Control: no-cache"));
  client.println(F("Connection: close"));
  client.println();

  // JSON body
  client.println(F("{"));

  client.println(F("  \"device\": {"));
  client.println(F("    \"name\": \"water_tank\","));
  client.println(F("    \"version\": \"1.4\","));

  client.print(F("    \"uptime_seconds\": "));
  client.println(millis() / 1000UL);

  client.println(F("  },"));

  client.println(F("  \"calibration\": {"));

  client.print(F("    \"adc_empty\": "));
  client.print(adcEmpty);
  client.println(F(","));

  client.print(F("    \"adc_full\": "));
  client.print(adcFull);
  client.println(F(","));

  client.print(F("    \"valid\": "));
  client.println(
    calibrationValid ? F("true") : F("false")
  );

  client.println(F("  },"));

  client.println(F("  \"sensor\": {"));

  client.print(F("    \"adc\": "));
  client.print(adcValue);
  client.println(F(","));

  client.print(F("    \"voltage\": "));
  client.print(voltage, 3);
  client.println(F(","));

  client.print(F("    \"current_mA\": "));
  client.print(currentMilliampere, 3);
  client.println(F(","));

  client.print(F("    \"active\": "));
  client.println(
    sensorActive ? F("true") : F("false")
  );

  client.println(F("  },"));

  client.println(F("  \"water\": {"));

  client.print(F("    \"percentage\": "));
  client.print(percentage, 1);
  client.println(F(","));

  client.print(F("    \"height_cm\": "));
  client.print(waterHeightCm, 1);
  client.println(F(","));

  client.print(F("    \"height_m\": "));
  client.print(waterHeightMeter, 3);
  client.println(F(","));

  client.print(F("    \"liters\": "));
  client.println(liters, 1);

  client.println(F("  }"));
  client.println(F("}"));

  printValuesToSerial(
    adcValue,
    voltage,
    currentMilliampere,
    percentage,
    waterHeightCm,
    liters
  );
}

// =====================================================
// SERIAL MONITOR OUTPUT
// =====================================================

void printValuesToSerial(
  int adcValue,
  float voltage,
  float currentMilliampere,
  float percentage,
  float waterHeightCm,
  float liters
) {
  Serial.print(F("ADC: "));
  Serial.print(adcValue);

  Serial.print(F(" | Voltage: "));
  Serial.print(voltage, 3);
  Serial.print(F(" V"));

  Serial.print(F(" | Current: "));
  Serial.print(currentMilliampere, 3);
  Serial.print(F(" mA"));

  Serial.print(F(" | Level: "));
  Serial.print(percentage, 1);
  Serial.print(F(" %"));

  Serial.print(F(" | Height: "));
  Serial.print(waterHeightCm, 1);
  Serial.print(F(" cm"));

  Serial.print(F(" | Volume: "));
  Serial.print(liters, 1);
  Serial.println(F(" L"));
}

// =====================================================
// AVERAGE ADC READING
// =====================================================

int readAverageAdc() {
  long total = 0;

  // Discard the first reading for a more stable ADC result.
  analogRead(sensorPin);
  delay(2);

  for (
    int measurement = 0;
    measurement < numberOfMeasurements;
    measurement++
  ) {
    total += analogRead(sensorPin);
    delay(measurementDelayMs);
  }

  return (int)(total / numberOfMeasurements);
}

// =====================================================
// VOLTAGE CALCULATION
// =====================================================

float calculateVoltage(int adcValue) {
  return adcValue *
         (arduinoReferenceVoltage / 1023.0);
}

// =====================================================
// CURRENT CALCULATION
// =====================================================

float calculateCurrentMilliampere(float voltage) {
  return (voltage / resistorOhm) * 1000.0;
}

// =====================================================
// PERCENTAGE CALCULATION
// =====================================================

float calculatePercentage(int adcValue) {
  if (adcFull <= adcEmpty) {
    return 0.0;
  }

  float percentage =
    ((float)(adcValue - adcEmpty) /
     (float)(adcFull - adcEmpty)) * 100.0;

  if (percentage < 0.0) {
    percentage = 0.0;
  }

  if (percentage > 100.0) {
    percentage = 100.0;
  }

  return percentage;
}

// =====================================================
// WATER HEIGHT CALCULATION
// =====================================================

float calculateWaterHeightCm(float percentage) {
  return tankHeightCm * percentage / 100.0;
}

// =====================================================
// VOLUME CALCULATION
// =====================================================

float calculateLiters(float percentage) {
  return tankCapacityLiters * percentage / 100.0;
}
