#include <Arduino.h>

// Pin definition
const int ANALOG_PIN = A7;

// Calibration / Reference voltage connected to AREF pin
const float V_REF = 3.30f;            // 3.3V connected to AREF
const float ADC_RESOLUTION = 1023.0f; // 10-bit ADC (0 - 1023)

void setup() {
  // 1. Set analog reference to EXTERNAL (connected to 3.3V on AREF pin)
  // WARNING: Ensure 3.3V is physically connected to the AREF pin and NOT 5V,
  // and analogReference(EXTERNAL) is called BEFORE any analogRead().
  analogReference(EXTERNAL);

  // Initialize Serial communication at 115200 baud
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
    // Wait briefly for serial to stabilize (useful on 32u4 / native USB)
  }

  // Discard first reading after changing voltage reference to let AREF settle
  analogRead(ANALOG_PIN);
  delay(10);
}

void loop() {
  // Read raw 10-bit ADC value (0 - 1023)
  int rawADC = analogRead(ANALOG_PIN);

  // Convert raw ADC reading to actual voltage based on 3.3V AREF
  float voltage = (rawADC * V_REF) / ADC_RESOLUTION;

  // Print formatted output compatible with the Web Force Bench serial plotter
  // Format: "Voltage: <value> V, Raw: <rawADC>"
  Serial.print("Voltage: ");
  Serial.print(voltage, 4); // 4 decimal places
  Serial.print(" V, Raw: ");
  Serial.println(rawADC);

  // Sample delay (adjust as needed, e.g. 50ms = 20Hz update rate)
  delay(50);
}
