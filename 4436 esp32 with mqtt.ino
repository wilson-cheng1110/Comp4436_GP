#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h> // MQTT client library

// Pin definitions
#define PIR_PIN 25        // PIR Sensor pin
#define DHT_PIN 33        // DHT11 Sensor pin
#define PHOTO_DO_PIN 34   // MH Photosensitivity Module Digital Output
#define PHOTO_AO_PIN 32   // MH Photosensitivity Module Analog Output
#define DHT_TYPE DHT11    // DHT 11
#define LED_PIN 26        // LED pin
#define SERVO_PIN 27      // Servo motor pin

// WiFi and MQTT configuration
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "MQTT_BROKER_IP"; // e.g., "192.168.1.100"
const int mqtt_port = 1883;
const char* mqtt_user = "MQTT_USERNAME";     // if required
const char* mqtt_pass = "MQTT_PASSWORD";     // if required

// MQTT topics
const char* topic_temp = "esp32/sensors/temperature";
const char* topic_humidity = "esp32/sensors/humidity";
const char* topic_light = "esp32/sensors/light";
const char* topic_occupancy = "esp32/sensors/occupancy";
const char* topic_led = "esp32/actuators/led";
const char* topic_servo = "esp32/actuators/servo";
const char* topic_servo_status = "esp32/status/servo";

// Function prototype for isNumeric
bool isNumeric(String str);

// Initialize sensors and servo
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo; // Create servo object

// Variable to store LED and servo states
bool ledState = LOW; // Initially LED is off
int servoAngle = 0; // Initial servo angle (0 degrees)

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      // Subscribe to topics for actuator control
      client.subscribe(topic_led);
      client.subscribe(topic_servo);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Handle LED control
  if (String(topic) == topic_led) {
    if (message.equalsIgnoreCase("ON")) {
      ledState = HIGH;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned ON via MQTT");
    } else if (message.equalsIgnoreCase("OFF")) {
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned OFF via MQTT");
    }
  }
  
  // Handle Servo control
  if (String(topic) == topic_servo) {
    if (isNumeric(message)) {
      int angle = message.toInt();
      if (angle >= 0 && angle <= 180) {
        servoAngle = angle;
        myServo.write(servoAngle);
        Serial.print("Servo set to ");
        Serial.print(servoAngle);
        Serial.println(" degrees via MQTT");
        
        // Publish servo status
        String status;
        if (servoAngle >= 0 && servoAngle <= 45) {
          status = "Closed";
        } else if (servoAngle >= 46 && servoAngle <= 135) {
          status = "Half-closed";
        } else {
          status = "Open";
        }
        client.publish(topic_servo_status, status.c_str());
      }
    }
  }
}

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

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("ESP32 Sensor System Initialized with MQTT!");
  Serial.println("Commands:");
  Serial.println(" - 'ON'/'OFF' to turn LED on/off");
  Serial.println(" - '0-180' to set servo angle (0-45: Closed, 46-135: Half-closed, 136-180: Open)");
  delay(1000); // Wait for sensors to stabilize
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Check for Serial input to control LED or Servo
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n'); // Read command from Serial Monitor
    command.trim(); // Remove any whitespace or newline

    // Check if command is for LED
    if (command.equalsIgnoreCase("ON")) {
      ledState = HIGH;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned ON");
      client.publish(topic_led, "ON");
    } else if (command.equalsIgnoreCase("OFF")) {
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
      Serial.println("LED turned OFF");
      client.publish(topic_led, "OFF");
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

        // Publish servo angle to MQTT
        char angleStr[4];
        itoa(servoAngle, angleStr, 10);
        client.publish(topic_servo, angleStr);

        // Print and publish status based on angle
        String status;
        if (servoAngle >= 0 && servoAngle <= 45) {
          status = "Closed";
          Serial.println("Status: Closed");
        } else if (servoAngle >= 46 && servoAngle <= 135) {
          status = "Half-closed";
          Serial.println("Status: Half-closed");
        } else if (servoAngle >= 136 && servoAngle <= 180) {
          status = "Open";
          Serial.println("Status: Open");
        }
        client.publish(topic_servo_status, status.c_str());
      } else {
        Serial.println("Invalid angle! Use 0-180 degrees");
      }
    } else {
      Serial.println("Invalid command! Use 'ON', 'OFF', or 0-180 for servo angle");
    }
  }

  // Read sensors and publish data
  readAndPublishSensors();
  
  delay(2000); // Wait 2 seconds before next reading
}

void readAndPublishSensors() {
  // Read PIR Sensor (Occupancy)
  int occupancy = digitalRead(PIR_PIN);
  Serial.print("Occupancy: ");
  if (occupancy == HIGH) {
    Serial.println("Detected (Movement)");
    client.publish(topic_occupancy, "Detected");
  } else {
    Serial.println("No Movement");
    client.publish(topic_occupancy, "No Movement");
  }

  // Read MH Photosensitivity Module
  int lightAnalog = analogRead(PHOTO_AO_PIN); // Analog output (0-4095 for ESP32)
  
  Serial.print("Brightness Value: ");
  Serial.println(lightAnalog); // Raw analog value (0-4095)
  
  // Publish light value
  char lightStr[6];
  itoa(lightAnalog, lightStr, 10);
  client.publish(topic_light, lightStr);

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
    
    // Publish temperature and humidity
    char tempStr[6];
    dtostrf(temperature, 4, 2, tempStr);
    client.publish(topic_temp, tempStr);
    
    char humStr[6];
    dtostrf(humidity, 4, 2, humStr);
    client.publish(topic_humidity, humStr);
  }

  // Publish current LED and Servo states
  client.publish(topic_led, ledState ? "ON" : "OFF");
  
  char angleStr[4];
  itoa(servoAngle, angleStr, 10);
  client.publish(topic_servo, angleStr);
  
  String status;
  if (servoAngle >= 0 && servoAngle <= 45) {
    status = "Closed";
  } else if (servoAngle >= 46 && servoAngle <= 135) {
    status = "Half-closed";
  } else {
    status = "Open";
  }
  client.publish(topic_servo_status, status.c_str());
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
