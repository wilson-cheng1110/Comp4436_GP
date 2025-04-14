#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Pin definitions
#define PIR_PIN 25
#define DHT_PIN 33
#define PHOTO_DO_PIN 34
#define PHOTO_AO_PIN 32
#define DHT_TYPE DHT11
#define LED_PIN 26
#define SERVO_PIN 27

// WiFi Enterprise configuration (for PolyUWAN)
const char* ssid = "PolyUWAN";
const char* wifi_username = "YOUR_POLYU_USERNAME"; // e.g., student ID
const char* wifi_password = "YOUR_POLYU_PASSWORD"; // Your PolyU WiFi password

// MQTT configuration (no authentication needed)
const char* mqtt_server = "MQTT_BROKER_IP"; // Replace with your broker IP
const int mqtt_port = 1883;

// MQTT topics
const char* topic_temp = "esp32/sensors/temperature";
const char* topic_humidity = "esp32/sensors/humidity";
const char* topic_light = "esp32/sensors/light";
const char* topic_occupancy = "esp32/sensors/occupancy";
const char* topic_led = "esp32/actuators/led";
const char* topic_servo = "esp32/actuators/servo";
const char* topic_servo_status = "esp32/status/servo";

// Initialize components
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo;
bool ledState = LOW;
int servoAngle = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Special configuration for enterprise network
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)wifi_username, strlen(wifi_username));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t *)wifi_username, strlen(wifi_username));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t *)wifi_password, strlen(wifi_password));
  esp_wifi_sta_wpa2_ent_enable();

  WiFi.begin(ssid); // No password parameter for enterprise network

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
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Connect without username/password
    if (client.connect("ESP32Client")) {
      Serial.println("connected");
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
  
  if (String(topic) == topic_servo) {
    if (isNumeric(message)) {
      int angle = message.toInt();
      if (angle >= 0 && angle <= 180) {
        servoAngle = angle;
        myServo.write(servoAngle);
        Serial.print("Servo set to ");
        Serial.print(servoAngle);
        Serial.println(" degrees via MQTT");
        
        String status;
        if (servoAngle <= 45) status = "Closed";
        else if (servoAngle <= 135) status = "Half-closed";
        else status = "Open";
        client.publish(topic_servo_status, status.c_str());
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(PHOTO_DO_PIN, INPUT);
  pinMode(PHOTO_AO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);
  dht.begin();
  myServo.attach(SERVO_PIN);
  myServo.write(servoAngle);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("ESP32 Sensor System Initialized with MQTT!");
  delay(1000);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

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
    } else if (isNumeric(command)) {
      int angle = command.toInt();
      if (angle >= 0 && angle <= 180) {
        servoAngle = angle;
        myServo.write(servoAngle);
        Serial.print("Servo set to: ");
        Serial.print(servoAngle);
        Serial.println(" degrees");

        char angleStr[4];
        itoa(servoAngle, angleStr, 10);
        client.publish(topic_servo, angleStr);

        String status;
        if (servoAngle <= 45) status = "Closed";
        else if (servoAngle <= 135) status = "Half-closed";
        else status = "Open";
        client.publish(topic_servo_status, status.c_str());
      }
    }
  }

  readAndPublishSensors();
  delay(2000);
}

void readAndPublishSensors() {
  // PIR Sensor
  int occupancy = digitalRead(PIR_PIN);
  client.publish(topic_occupancy, occupancy ? "Detected" : "No Movement");

  // Light Sensor
  int lightAnalog = analogRead(PHOTO_AO_PIN);
  char lightStr[6];
  itoa(lightAnalog, lightStr, 10);
  client.publish(topic_light, lightStr);

  // DHT Sensor
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  if (!isnan(humidity) && !isnan(temperature)) {
    char tempStr[6], humStr[6];
    dtostrf(temperature, 4, 2, tempStr);
    dtostrf(humidity, 4, 2, humStr);
    client.publish(topic_temp, tempStr);
    client.publish(topic_humidity, humStr);
  }

  // Device states
  client.publish(topic_led, ledState ? "ON" : "OFF");
  char angleStr[4];
  itoa(servoAngle, angleStr, 10);
  client.publish(topic_servo, angleStr);
}

bool isNumeric(String str) {
  for (int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}
