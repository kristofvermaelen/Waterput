#include <SPI.h>
#include <Ethernet.h>

// =====================================================
// NETWERKINSTELLINGEN
// =====================================================

byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

IPAddress ip(172, 16, 42, 66);

EthernetServer server(80);

// =====================================================
// SENSORINSTELLINGEN
// =====================================================

const int sensorPin = A0;

// Weerstand waarmee 4-20 mA wordt omgezet naar spanning
const float weerstandOhm = 220.0;

// Gemeten voedingsspanning van de Arduino.
// Voorlopig gebruiken we 5,00 V.
// Later kunnen we dit nauwkeuriger meten met de multimeter.
const float arduinoReferentieSpanning = 5.0;

// =====================================================
// KALIBRATIE
// =====================================================

// Vul hier later de gemeten ADC-waarde in wanneer de tank leeg is.
const int adcLeeg = 196;

// Vul hier later de gemeten ADC-waarde in wanneer de tank vol is.
// 820 is voorlopig slechts een voorbeeldwaarde.
const int adcVol = 820;

// Werkelijke waterhoogte wanneer de tank volledig vol is.
const float tankHoogteCm = 250.0;

// Totale tankinhoud.
// Alleen correct voor liters als het volume lineair met de hoogte stijgt,
// bijvoorbeeld bij een rechte cilindrische of rechthoekige tank.
const float tankInhoudLiter = 10000.0;

// =====================================================
// MEETINSTELLINGEN
// =====================================================

// Hoeveel metingen worden gemiddeld.
const int aantalMetingen = 20;

// Tijd tussen de afzonderlijke metingen.
const int meetPauzeMs = 5;

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(9600);

  Serial.println();
  Serial.println("Waterput monitor start...");

  Ethernet.begin(mac, ip);

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("FOUT: Ethernet shield niet gevonden.");

    while (true) {
      delay(1);
    }
  }

  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("WAARSCHUWING: Ethernetkabel niet aangesloten.");
  }

  server.begin();

  Serial.print("Webserver actief op: http://");
  Serial.println(Ethernet.localIP());
}

// =====================================================
// HOOFDPROGRAMMA
// =====================================================

void loop() {
  EthernetClient client = server.available();

  if (!client) {
    return;
  }

  Serial.println("Nieuwe client verbonden.");

  bool currentLineIsBlank = true;
  unsigned long startTijd = millis();

  while (client.connected()) {

    // Stoppen wanneer de client langer dan twee seconden niets doet.
    if (millis() - startTijd > 2000) {
      Serial.println("Client timeout.");
      break;
    }

    if (client.available()) {
      char c = client.read();

      // Een lege regel betekent dat de HTTP-request volledig ontvangen is.
      if (c == '\n' && currentLineIsBlank) {
        stuurJsonAntwoord(client);
        break;
      }

      if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }

  delay(1);
  client.stop();

  Serial.println("Client losgekoppeld.");
}

// =====================================================
// JSON ANTWOORD
// =====================================================

void stuurJsonAntwoord(EthernetClient &client) {
  int adcWaarde = leesGemiddeldeADC();

  float spanning =
    adcWaarde * (arduinoReferentieSpanning / 1023.0);

  float stroomMilliampere =
    (spanning / weerstandOhm) * 1000.0;

  float percentage = berekenPercentage(adcWaarde);

  float waterhoogteCm =
    tankHoogteCm * percentage / 100.0;

  float waterhoogteMeter =
    waterhoogteCm / 100.0;

  float liters =
    tankInhoudLiter * percentage / 100.0;

  bool sensorActief =
    stroomMilliampere >= 3.5;

  bool kalibratieGeldig =
    adcVol > adcLeeg;

  // HTTP-header
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json; charset=utf-8");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  // JSON
  client.println("{");

  client.println("  \"device\": {");
  client.println("    \"name\": \"waterput\",");
  client.println("    \"version\": \"1.1\",");
  client.print("    \"uptime_seconds\": ");
  client.println(millis() / 1000);
  client.println("  },");

  client.println("  \"calibration\": {");

  client.print("    \"adc_empty\": ");
  client.print(adcLeeg);
  client.println(",");

  client.print("    \"adc_full\": ");
  client.print(adcVol);
  client.println(",");

  client.print("    \"valid\": ");
  client.println(kalibratieGeldig ? "true" : "false");

  client.println("  },");

  client.println("  \"sensor\": {");

  client.print("    \"adc\": ");
  client.print(adcWaarde);
  client.println(",");

  client.print("    \"voltage\": ");
  client.print(spanning, 3);
  client.println(",");

  client.print("    \"current_mA\": ");
  client.print(stroomMilliampere, 3);
  client.println(",");

  client.print("    \"active\": ");
  client.println(sensorActief ? "true" : "false");

  client.println("  },");

  client.println("  \"water\": {");

  client.print("    \"percentage\": ");
  client.print(percentage, 1);
  client.println(",");

  client.print("    \"height_cm\": ");
  client.print(waterhoogteCm, 1);
  client.println(",");

  client.print("    \"height_m\": ");
  client.print(waterhoogteMeter, 3);
  client.println(",");

  client.print("    \"liters\": ");
  client.print(liters, 1);

  client.println();
  client.println("  }");

  client.println("}");

  // Ook tonen in de seriële monitor
  Serial.print("ADC: ");
  Serial.print(adcWaarde);

  Serial.print(" | Stroom: ");
  Serial.print(stroomMilliampere, 3);
  Serial.print(" mA");

  Serial.print(" | Niveau: ");
  Serial.print(percentage, 1);
  Serial.print(" %");

  Serial.print(" | Hoogte: ");
  Serial.print(waterhoogteCm, 1);
  Serial.println(" cm");
}

// =====================================================
// ADC GEMIDDELDE
// =====================================================

int leesGemiddeldeADC() {
  long totaal = 0;

  // Eerste uitlezing weggooien voor een stabielere ADC-meting.
  analogRead(sensorPin);
  delay(2);

  for (int i = 0; i < aantalMetingen; i++) {
    totaal += analogRead(sensorPin);
    delay(meetPauzeMs);
  }

  return (int)(totaal / aantalMetingen);
}

// =====================================================
// PERCENTAGE BEREKENEN
// =====================================================

float berekenPercentage(int adcWaarde) {
  // Bescherming tegen een foutieve kalibratie.
  if (adcVol <= adcLeeg) {
    return 0.0;
  }

  float percentage =
    ((float)(adcWaarde - adcLeeg) /
    (float)(adcVol - adcLeeg)) * 100.0;

  // Alles onder de leegwaarde wordt 0%.
  if (percentage < 0.0) {
    percentage = 0.0;
  }

  // Alles boven de volwaarde wordt 100%.
  if (percentage > 100.0) {
    percentage = 100.0;
  }

  return percentage;
}
