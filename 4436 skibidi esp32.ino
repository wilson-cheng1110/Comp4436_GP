#include <DHT.h>

// Pin definitions
#define PIR_PIN 25    // PIR Sensor pin
#define DHT_PIN 33    // DHT11 Sensor pin
#define PHOTO_DO_PIN 34 // MH Photosensitivity Module Digital Output
#define PHOTO_AO_PIN 32 // MH Photosensitivity Module Analog Output
#define DHT_TYPE DHT11 // DHT 11

// Initialize DHT sensor
DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(115200); // Start serial communication
  pinMode(PIR_PIN, INPUT); // Set PIR pin as input
  pinMode(PHOTO_DO_PIN, INPUT); // Set Photosensitivity Digital Output as input
  pinMode(PHOTO_AO_PIN, INPUT); // Set Photosensitivity Analog Output as input
  dht.begin(); // Start DHT sensor

  Serial.println("ESP32 Sensor System Initialized!");
  delay(1000); // Wait for sensors to stabilize
}

void loop() {
  // Read PIR Sensor (Occupancy)
  int occupancy = digitalRead(PIR_PIN);
  Serial.print("Occupancy: ");
  if (occupancy == HIGH) {
    Serial.println("Detected (Movement)");
  } else {
    Serial.println("No Movement");
  }

  // Read MH Photosensitivity Module
  int lightDigital = digitalRead(PHOTO_DO_PIN); // Digital output (HIGH/LOW based on threshold)
  int lightAnalog = analogRead(PHOTO_AO_PIN); // Analog output (0-4095 for ESP32)

  Serial.print("Light Digital Output: ");
  if (lightDigital == HIGH) {
    Serial.println("Bright (Above Threshold)");
  } else {
    Serial.println("Dark (Below Threshold)");
  }

  // Print Brightness Value (using Analog Output)
  Serial.print("Brightness Value: ");
  Serial.println(lightAnalog); // Raw analog value (0-4095)

  // Read DHT11 (Temperature and Humidity)
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.print("%  Temperature: ");
    Serial.print(temperature);
    Serial.println("°C");
  }

  Serial.println("------------------------");
  delay(2000); // Wait 2 seconds before next reading
}