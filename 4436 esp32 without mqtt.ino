#include <DHT.h>
#include <ESP32Servo.h> // Add Servo library

// Pin definitions
#define PIR_PIN 25        // PIR Sensor pin
#define DHT_PIN 33        // DHT11 Sensor pin
#define PHOTO_DO_PIN 34   // MH Photosensitivity Module Digital Output
#define PHOTO_AO_PIN 32   // MH Photosensitivity Module Analog Output
#define DHT_TYPE DHT11    // DHT 11
#define LED_PIN 26        // LED pin
#define SERVO_PIN 27      // Servo motor pin

// Function prototype for isNumeric
bool isNumeric(String str);

// Initialize sensors and servo
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo; // Create servo object

// Variable to store LED and servo states
bool ledState = LOW; // Initially LED is off
int servoAngle = 0; // Initial servo angle (0 degrees)

void setup() {
  Serial.begin(115200); // Start serial communication
  pinMode(PIR_PIN, INPUT); // Set PIR pin as input
  pinMode(PHOTO_DO_PIN, INPUT); // Set Photosensitivity Digital Output as input
  pinMode(PHOTO_AO_PIN, INPUT); // Set Photosensitivity Analog Output as input
  pinMode(LED_PIN, OUTPUT); // Set LED pin as output
  digitalWrite(LED_PIN, ledState); // Initialize LED state
  dht.begin(); // Start DHT sensor
  myServo.attach(SERVO_PIN); // Attach servo to pin
  myServo.write(servoAngle); // Initialize servo at 0 degrees

  Serial.println("ESP32 Sensor System Initialized!");
  Serial.println("Commands:");
  Serial.println(" - 'ON'/'OFF' to turn LED on/off");
  Serial.println(" - '0-180' to set servo angle (0-45: Closed, 46-135: Half-closed, 136-180: Open)");
  delay(1000); // Wait for sensors to stabilize
}

void loop() {
  // Check for Serial input to control LED or Servo
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n'); // Read command from Serial Monitor
    command.trim(); // Remove any whitespace or newline

    // Check if command is for LED
    if (command.equalsIgnoreCase("ON")) {
      ledState = HIGH;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned ON");
    } else if (command.equalsIgnoreCase("OFF")) {
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned OFF");
    } 
    // Check if command is a number (for servo angle)
    else if (isNumeric(command)) {
      int angle = command.toInt(); // Convert string to integer

      // Validate angle range (0-180)
      if (angle >= 0 && angle <= 180) {
        servoAngle = angle;
        myServo.write(servoAngle); // Set servo to the new angle
        Serial.print("Servo set to: ");
        Serial.print(servoAngle);
        Serial.println(" degrees");

        // Print status based on angle
        if (servoAngle >= 0 && servoAngle <= 45) {
          Serial.println("Status: Closed");
        } else if (servoAngle >= 46 && servoAngle <= 135) {
          Serial.println("Status: Half-closed");
        } else if (servoAngle >= 136 && servoAngle <= 180) {
          Serial.println("Status: Open");
        }
      } else {
        Serial.println("Invalid angle! Use 0-180 degrees");
      }
    } else {
      Serial.println("Invalid command! Use 'ON', 'OFF', or 0-180 for servo angle");
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
  if (lightAnalog < 2000) {
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

  // Print current LED and Servo states
  Serial.print("LED State: ");
  if (ledState == HIGH) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }

  Serial.print("Servo Angle: ");
  Serial.print(servoAngle);
  Serial.println(" degrees");

  // Print current servo status
  if (servoAngle >= 0 && servoAngle <= 45) {
    Serial.println("Curtain Status: Closed");
  } else if (servoAngle >= 46 && servoAngle <= 135) {
    Serial.println("Curtain Status: Half-closed");
  } else if (servoAngle >= 136 && servoAngle <= 180) {
    Serial.println("Curtain Status: Open");
  }

  Serial.println("------------------------");
  delay(2000); // Wait 2 seconds before next reading
}

// Function definition for isNumeric
bool isNumeric(String str) {
  for (int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}
