#include <WiFi.h>         // For WiFi connectivity
#include <WebServer.h>    // *** WEB SERVER: Include the WebServer library
#include <PubSubClient.h> // For MQTT communication
#include <ArduinoJson.h>  // For formatting data as JSON
#include <DHT.h>
#include <ESP32Servo.h> // Add Servo library

// --- WiFi Configuration ---
const char* ssid = "D730";         // <<<--- REPLACE with your Hotspot Name
const char* password = "11111111"; // <<<--- REPLACE with your Hotspot Password

// --- MQTT Configuration ---
const char* mqtt_server = "test.mosquitto.org"; // Public MQTT broker
const int mqtt_port = 1883;                   // Default MQTT port (unencrypted)
const char* mqtt_topic = "/home/sensors";       // Topic to publish sensor data
const char* mqtt_client_id_base = "ESP32_SensorNode_"; // Base for unique client ID

// --- Pin definitions ---
#define PIR_PIN 25        // PIR Sensor pin
#define DHT_PIN 33        // DHT11 Sensor pin
#define PHOTO_DO_PIN 34   // MH Photosensitivity Module Digital Output
#define PHOTO_AO_PIN 32   // MH Photosensitivity Module Analog Output
#define DHT_TYPE DHT11    // DHT 11
#define LED_PIN 26        // LED pin
#define SERVO_PIN 27      // Servo motor pin

// --- Function prototype for isNumeric ---
bool isNumeric(String str);

// --- Initialize sensors and servo ---
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo; // Create servo object

// --- Variable to store LED and servo states ---
// These are now controlled by Serial, MQTT (if subscribed), AND Web Server
volatile bool ledState = LOW; // Initially LED is off (volatile might be good practice if used across interrupts, though not strictly needed here)
volatile int servoAngle = 0; // Initial servo angle (0 degrees)

// --- WiFi and MQTT Objects ---
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0; // Timer for MQTT publishing
#define MSG_PUBLISH_INTERVAL 5000 // Publish data every 5 seconds (5000ms)

// --- Web Server Object ---
// *** WEB SERVER: Create a WebServer object listening on port 80 (standard HTTP port)
WebServer server(80);

// --- WiFi Setup Function ---
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP()); // Print the IP address! Needed for web requests.
  } else {
    Serial.println("WiFi connection failed. Check credentials or signal.");
  }
}

// --- MQTT Reconnect Function ---
void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = mqtt_client_id_base;
    clientId += String(WiFi.macAddress());
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


// --- Web Server Handler Functions ---

// *** WEB SERVER: Handler for the root URL ("/") - Provides status and instructions
void handleRoot() {
  String html = "<html><head><title>ESP32 Control</title></head><body>";
  html += "<h1>ESP32 Control Panel</h1>";
  html += "<p>Current LED State: ";
  html += (ledState == HIGH) ? "ON" : "OFF";
  html += "</p>";
  html += "<p>Current Servo Angle: " + String(servoAngle) + " degrees</p>";
  html += "<h2>Commands:</h2>";
  html += "<ul>";
  html += "<li>Turn LED ON: <a href='/led/on'>/led/on</a></li>";
  html += "<li>Turn LED OFF: <a href='/led/off'>/led/off</a></li>";
  html += "<li>Set Servo Angle (e.g., 90 degrees): /servo?angle=90</li>";
  html += "</ul>";
  html += "</body></html>";
  server.send(200, "text/html", html); // Send HTTP status 200 (OK) and HTML content
}

// *** WEB SERVER: Handler for turning the LED ON ("/led/on")
void handleLedOn() {
  ledState = HIGH;
  digitalWrite(LED_PIN, ledState);
  Serial.println("HTTP Command received: /led/on");
  server.send(200, "text/plain", "OK - LED turned ON"); // Send simple text confirmation
  // Status will be updated in next MQTT message automatically
}

// *** WEB SERVER: Handler for turning the LED OFF ("/led/off")
void handleLedOff() {
  ledState = LOW;
  digitalWrite(LED_PIN, ledState);
  Serial.println("HTTP Command received: /led/off");
  server.send(200, "text/plain", "OK - LED turned OFF");
  // Status will be updated in next MQTT message automatically
}

// *** WEB SERVER: Handler for setting the Servo angle ("/servo?angle=VALUE")
void handleServo() {
  String angleStr = "";
  int requestedAngle = -1; // Use -1 to indicate invalid/not found initially

  if (server.hasArg("angle")) { // Check if the 'angle' parameter exists
    angleStr = server.arg("angle");
    if (isNumeric(angleStr)) {
        requestedAngle = angleStr.toInt();
    }
  }

  if (requestedAngle >= 0 && requestedAngle <= 180) {
    servoAngle = requestedAngle; // Update the global servo angle
    myServo.write(servoAngle);   // Move the servo
    Serial.print("HTTP Command received: /servo?angle=");
    Serial.println(servoAngle);
    server.send(200, "text/plain", "OK - Servo set to " + String(servoAngle) + " degrees");
    // Status will be updated in next MQTT message automatically
  } else {
    // Send an error response if angle is missing, not numeric, or out of range
    Serial.println("HTTP Command error: Invalid or missing 'angle' parameter for /servo");
    server.send(400, "text/plain", "Bad Request: Invalid or missing 'angle' parameter (must be 0-180). Usage: /servo?angle=90"); // HTTP 400 Bad Request
  }
}

// *** WEB SERVER: Handler for requests to unknown paths
void handleNotFound() {
  server.send(404, "text/plain", "Not Found"); // Send HTTP status 404
}


// --- Setup Function ---
void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(PIR_PIN, INPUT);
  pinMode(PHOTO_DO_PIN, INPUT);
  pinMode(PHOTO_AO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState); // Initialize LED state

  dht.begin();

  myServo.attach(SERVO_PIN);
  myServo.write(servoAngle); // Initialize servo

  Serial.println("ESP32 Sensor System Initializing...");

  setup_wifi(); // Connect to WiFi

  // Configure MQTT client (only if WiFi connected)
  if (WiFi.status() == WL_CONNECTED) {
    client.setServer(mqtt_server, mqtt_port);
  }

  // *** WEB SERVER: Define Routes (Endpoints) - Do this AFTER WiFi connects
  if (WiFi.status() == WL_CONNECTED) {
      server.on("/", HTTP_GET, handleRoot);         // Route for the root URL "/"
      server.on("/led/on", HTTP_GET, handleLedOn);   // Route for "/led/on"
      server.on("/led/off", HTTP_GET, handleLedOff); // Route for "/led/off"
      server.on("/servo", HTTP_GET, handleServo);    // Route for "/servo?angle=..."

      server.onNotFound(handleNotFound); // Handler for unknown paths

      // Start the server
      server.begin();
      Serial.println("HTTP server started. Access at http://" + WiFi.localIP().toString() + "/");
  } else {
      Serial.println("HTTP server NOT started (WiFi not connected).");
  }


  Serial.println("System Initialized!");
  Serial.println("Commands via Serial Monitor:");
  Serial.println(" - 'ON'/'OFF' to turn LED on/off");
  Serial.println(" - '0-180' to set servo angle");
  Serial.println("Commands via HTTP GET requests:");
  Serial.println(" - http://<IP>/led/on");
  Serial.println(" - http://<IP>/led/off");
  Serial.println(" - http://<IP>/servo?angle=<0-180>");
  delay(1000);
}

// --- Main Loop ---
void loop() {
  // --- WiFi/MQTT Connection Maintenance ---
  if (WiFi.status() != WL_CONNECTED) {
     Serial.println("WiFi disconnected. Attempting reconnect...");
     setup_wifi(); // Try to reconnect WiFi
     // If WiFi reconnects, MQTT will try to reconnect below.
     // Server won't automatically restart here, might need manual reset or more complex logic if needed
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnect_mqtt(); // Reconnect to MQTT if disconnected
    }
    client.loop(); // Allow the MQTT client to process

    // *** WEB SERVER: Handle incoming client requests
    server.handleClient();
  }


  // --- Serial Command Handling --- (Remains functional)
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.equalsIgnoreCase("ON")) {
      ledState = HIGH;
      digitalWrite(LED_PIN, ledState);
      Serial.println("Serial Cmd: LED turned ON");
    } else if (command.equalsIgnoreCase("OFF")) {
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
      Serial.println("Serial Cmd: LED turned OFF");
    } else if (isNumeric(command)) {
      int angle = command.toInt();
      if (angle >= 0 && angle <= 180) {
        servoAngle = angle;
        myServo.write(servoAngle);
        Serial.print("Serial Cmd: Servo set to ");
        Serial.print(servoAngle);
        Serial.println(" degrees");
      } else {
        Serial.println("Serial Cmd: Invalid angle! Use 0-180 degrees");
      }
    } else {
      Serial.println("Serial Cmd: Invalid command! Use 'ON', 'OFF', or 0-180");
    }
  }

  // --- Sensor Reading and MQTT Publishing (Timed) ---
  unsigned long now = millis();
  if (now - lastMsg > MSG_PUBLISH_INTERVAL && WiFi.status() == WL_CONNECTED && client.connected()) {
    lastMsg = now;

    // Read Sensors
    int occupancy_raw = digitalRead(PIR_PIN);
    String occupancy_state = (occupancy_raw == HIGH) ? "Detected" : "No Movement";
    int lightAnalog = analogRead(PHOTO_AO_PIN);
    String light_state = (lightAnalog < 2000) ? "Bright" : "Dark";
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    String servo_status;
    if (servoAngle >= 0 && servoAngle <= 45) servo_status = "Closed";
    else if (servoAngle >= 46 && servoAngle <= 135) servo_status = "Half-closed";
    else servo_status = "Open";

    // Print sensor values to Serial Monitor (Condensed for brevity)
    Serial.println("--- Sensor Readings & Status ---");
    Serial.printf("Occupancy: %s, Light: %s (%d), Temp: %.1fC, Humid: %.1f%%\n",
                  occupancy_state.c_str(), light_state.c_str(), lightAnalog, temperature, humidity);
    Serial.printf("LED: %s, Servo: %d deg (%s)\n",
                  (ledState ? "ON" : "OFF"), servoAngle, servo_status.c_str());
    Serial.println("-----------------------------");


    // --- Prepare JSON Payload ---
    StaticJsonDocument<256> doc;
    doc["occupancy"] = occupancy_state;
    doc["brightness_raw"] = lightAnalog;
    doc["light_state"] = light_state;
    if (!isnan(humidity)) doc["humidity"] = humidity;
    if (!isnan(temperature)) doc["temperature"] = temperature;
    doc["led_state"] = ledState ? "ON" : "OFF"; // Use the current state
    doc["servo_angle"] = servoAngle;            // Use the current angle
    doc["curtain_status"] = servo_status;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    // --- Publish to MQTT ---
    Serial.print("Publishing to MQTT: "); Serial.println(mqtt_topic);
    // Serial.println(jsonBuffer); // Optional: print JSON again

    if (!client.publish(mqtt_topic, jsonBuffer)) {
       Serial.println("MQTT Publish failed");
    }
     Serial.println("-----------------------------");
  } else if (now - lastMsg > MSG_PUBLISH_INTERVAL && (WiFi.status() != WL_CONNECTED || !client.connected())) {
       // If it's time to publish but we're not connected, just reset timer and print status
       lastMsg = now;
       Serial.println("Cannot publish MQTT (WiFi or MQTT client disconnected).");
  }


  // Small delay - server.handleClient() needs to run frequently
  delay(10); // Reduced delay slightly
}

// --- Function definition for isNumeric --- (Unchanged)
bool isNumeric(String str) {
  if (str.length() == 0) return false;
  for (int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}
