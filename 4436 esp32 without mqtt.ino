#include <DHT.h>

// Pin definitions
#define PIR_PIN 25        // PIR Sensor pin
#define DHT_PIN 33        // DHT11 Sensor pin
#define PHOTO_DO_PIN 34   // MH Photosensitivity Module Digital Output
#define PHOTO_AO_PIN 32   // MH Photosensitivity Module Analog Output
#define DHT_TYPE DHT11    // DHT 11
#define LED_PIN 26        // LED pin

// Initialize DHT sensor
DHT dht(DHT_PIN, DHT_TYPE);

// Variable to store LED state
bool ledState = LOW; // Initially LED is off

void setup() {
  Serial.begin(115200); // Start serial communication
  pinMode(PIR_PIN, INPUT); // Set PIR pin as input
  pinMode(PHOTO_DO_PIN, INPUT); // Set Photosensitivity Digital Output as input
  pinMode(PHOTO_AO_PIN, INPUT); // Set Photosensitivity Analog Output as input
  pinMode(LED_PIN, OUTPUT); // Set LED pin as output
  digitalWrite(LED_PIN, ledState); // Initialize LED state
  dht.begin(); // Start DHT sensor

  Serial.println("ESP32 Sensor System Initialized!");
  Serial.println("Enter 'ON' to turn LED on, 'OFF' to turn LED off");
  delay(1000); // Wait for sensors to stabilize
}

void loop() {
  // Check for Serial input to control LED
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n'); // Read command from Serial Monitor
    command.trim(); // Remove any whitespace or newline

    if (command.equalsIgnoreCase("ON")) {
      ledState = HIGH;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned ON");
    } else if (command.equalsIgnoreCase("OFF")) {
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned OFF");
    } else {
      Serial.println("Invalid command! Use 'ON' or 'OFF'");
    }
  }

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
  if (lightAnalog<2000) {
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

  // Print current LED state
  Serial.print("LED State: ");
  if (ledState == HIGH) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }

  Serial.println("------------------------");
  delay(2000); // Wait 2 seconds before next reading
}