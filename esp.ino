#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP32Servo.h>

// --- WiFi Configuration ---
const char* ssid = "D730";         // <<<--- YOUR WIFI SSID
const char* password = "11111111"; // <<<--- YOUR WIFI PASSWORD

// --- MQTT Configuration ---
const char* mqtt_server = "172.20.10.2"; // <<<--- YOUR MQTT BROKER IP
const int mqtt_port = 1883;
const char* mqtt_status_topic = "/home/sensors";      // Topic to PUBLISH sensor data and status
const char* mqtt_command_topic = "/home/actuators/command"; // Topic to SUBSCRIBE for commands (from AI/Backend)
const char* mqtt_client_id_base = "ESP32_SensorNode_";

// --- Pin definitions ---
#define PIR_PIN 25
#define DHT_PIN 33
#define PHOTO_DO_PIN 34
#define PHOTO_AO_PIN 32
#define DHT_TYPE DHT11
#define LED_PIN 26
#define SERVO_PIN 27

// --- Control Hierarchy & Timing Settings ---
#define CONTROL_MODE_AUTO   0
#define CONTROL_MODE_MANUAL 1
#define MANUAL_MODE_DURATION 30000 // How long manual control lasts (milliseconds) e.g., 30 seconds
#define PIR_MOTION_TIMEOUT 30000   // How long LED stays on after last PIR motion in Auto mode (ms)
#define MQTT_PUBLISH_INTERVAL 5000 // Publish status every 5 seconds (ms)

// --- Global State Variables ---
volatile bool ledState = LOW;
volatile int servoAngle = 0;
volatile unsigned long lastMotionTime = 0;
volatile bool lastPirState = LOW;

// Control Mode Tracking
volatile int ledControlMode = CONTROL_MODE_AUTO;
volatile unsigned long lastLedManualCommandTime = 0;
volatile int servoControlMode = CONTROL_MODE_AUTO;
volatile unsigned long lastServoManualCommandTime = 0;

// --- Sensor Objects ---
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo;

// --- WiFi, MQTT, Web Server Objects ---
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMqttPublish = 0;
WebServer server(80);

// --- Forward Declarations ---
bool isNumeric(String str);
void publishMqttStatus();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void setup_wifi();
void reconnect_mqtt();
void handleRoot();
void handleLedOn();
void handleLedOff();
void handleServo();
void handleForceAutoMode(); // <<<--- ADDED: Declaration for new handler
void handleNotFound();
void applyLedState(bool newState, bool isManual);
void applyServoAngle(int newAngle, bool isManual);

// --- WiFi Setup Function ---
// (No changes)
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
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("WiFi connection failed. Check credentials or signal.");
    }
}

// --- MQTT Reconnect Function ---
// (No changes)
void reconnect_mqtt() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = mqtt_client_id_base;
        clientId += String(WiFi.macAddress());
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            if(client.subscribe(mqtt_command_topic)) {
                 Serial.print("Subscribed to command topic: ");
                 Serial.println(mqtt_command_topic);
            } else {
                 Serial.println("Failed to subscribe to command topic!");
            }
            publishMqttStatus();
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}


// --- MQTT Callback Function ---
// (No changes)
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    Serial.println(message);

    if (strcmp(topic, mqtt_command_topic) == 0) {
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, message);

        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        const char* target = doc["target"];
        if (!target) {
            Serial.println("Command missing 'target'");
            return;
        }

        if (strcmp(target, "led") == 0) {
            if (ledControlMode == CONTROL_MODE_AUTO) {
                if (doc.containsKey("state")) {
                    const char* stateStr = doc["state"];
                    bool desiredState = strcasecmp(stateStr, "ON") == 0;
                    Serial.print("AI Command Received (Auto Mode): LED -> "); Serial.println(stateStr);
                    applyLedState(desiredState, false);
                } else {
                    Serial.println("LED command missing 'state' field");
                }
            } else {
                Serial.println("Ignoring AI LED command (Manual Mode Active)");
            }
        }
        else if (strcmp(target, "servo") == 0) {
            if (servoControlMode == CONTROL_MODE_AUTO) {
                if (doc.containsKey("angle")) {
                    int desiredAngle = doc["angle"];
                    if (desiredAngle >= 0 && desiredAngle <= 180) {
                         Serial.print("AI Command Received (Auto Mode): Servo -> "); Serial.println(desiredAngle);
                         applyServoAngle(desiredAngle, false);
                    } else {
                         Serial.println("AI command invalid angle received");
                    }
                } else {
                     Serial.println("Servo command missing 'angle' field");
                }
            } else {
                Serial.println("Ignoring AI Servo command (Manual Mode Active)");
            }
        }
        else {
            Serial.print("Unknown command target received: "); Serial.println(target);
        }
    }
}

// --- Helper function to apply LED state changes ---
// (No changes)
void applyLedState(bool newState, bool isManual) {
    if (ledState != newState) {
        ledState = newState;
        digitalWrite(LED_PIN, ledState);
        Serial.printf("State Changed: LED %s\n", ledState ? "ON" : "OFF");
        if (isManual) {
            Serial.println("LED: Entering Manual Mode");
            ledControlMode = CONTROL_MODE_MANUAL;
            lastLedManualCommandTime = millis();
        }
        publishMqttStatus();
    } else {
         if (isManual) {
             Serial.println("LED: Manual command matches current state, resetting manual timer.");
             if (ledControlMode != CONTROL_MODE_MANUAL) {
                 Serial.println("LED: Entering Manual Mode");
                 ledControlMode = CONTROL_MODE_MANUAL;
             }
             lastLedManualCommandTime = millis();
         }
    }
}

// --- Helper function to apply Servo angle changes ---
// (No changes)
void applyServoAngle(int newAngle, bool isManual) {
     if (newAngle < 0) newAngle = 0;
     if (newAngle > 180) newAngle = 180;

    if (servoAngle != newAngle) {
        servoAngle = newAngle;
        myServo.write(servoAngle);
        Serial.printf("State Changed: Servo %d degrees\n", servoAngle);
         if (isManual) {
            Serial.println("Servo: Entering Manual Mode");
            servoControlMode = CONTROL_MODE_MANUAL;
            lastServoManualCommandTime = millis();
        }
        publishMqttStatus();
    } else {
         if (isManual) {
             Serial.println("Servo: Manual command matches current state, resetting manual timer.");
              if (servoControlMode != CONTROL_MODE_MANUAL) {
                 Serial.println("Servo: Entering Manual Mode");
                 servoControlMode = CONTROL_MODE_MANUAL;
              }
             lastServoManualCommandTime = millis();
         }
    }
}

// --- Function to Publish MQTT Status ---
// (No changes)
void publishMqttStatus() {
    if (WiFi.status() != WL_CONNECTED || !client.connected()) {
        return;
    }
    int occupancy_raw = digitalRead(PIR_PIN);
    String occupancy_state = (occupancy_raw == HIGH) ? "Detected" : "No Movement";
    int lightAnalog = analogRead(PHOTO_AO_PIN);
    String light_state = (lightAnalog < 2000) ? "Bright" : "Dark";
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    String servo_status;
    if (servoAngle >= 0 && servoAngle <= 45) servo_status = "Closed";
    else if (servoAngle > 45 && servoAngle < 135) servo_status = "Half-Open";
    else servo_status = "Open";

    StaticJsonDocument<256> doc;
    doc["occupancy_raw"] = occupancy_raw;
    doc["occupancy_state"] = occupancy_state;
    doc["brightness_raw"] = lightAnalog;
    doc["light_state"] = light_state;
    if (!isnan(humidity)) doc["humidity"] = round(humidity * 10.0) / 10.0; else doc["humidity"] = nullptr;
    if (!isnan(temperature)) doc["temperature"] = round(temperature * 10.0) / 10.0; else doc["temperature"] = nullptr;
    doc["led_state"] = ledState ? "ON" : "OFF";
    doc["led_mode"] = (ledControlMode == CONTROL_MODE_MANUAL) ? "Manual" : "Auto";
    doc["servo_angle"] = servoAngle;
    doc["curtain_status"] = servo_status;
    doc["servo_mode"] = (servoControlMode == CONTROL_MODE_MANUAL) ? "Manual" : "Auto";

    char jsonBuffer[256];
    size_t n = serializeJson(doc, jsonBuffer);
    if (!client.publish(mqtt_status_topic, jsonBuffer, n)) {
       Serial.println("MQTT Publish failed");
    }
}

// --- Web Server Handler Functions ---

// Root handler - Add the new button
void handleRoot() {
    String current_servo_status;
     if (servoAngle >= 0 && servoAngle <= 45) current_servo_status = "Closed";
    else if (servoAngle > 45 && servoAngle < 135) current_servo_status = "Half-Open";
    else current_servo_status = "Open";

    String html = "<html><head><title>ESP32 Control</title>";
    html += "<meta http-equiv='refresh' content='10'>";
    html += "<style> body { font-family: sans-serif; max-width: 600px; margin: auto; padding: 20px; }";
    html += " strong { color: #007bff; } li { margin-bottom: 10px; list-style: none; padding-left: 0;}";
    html += " button, input[type=submit] { padding: 8px 15px; cursor: pointer; margin-right: 5px;}"; // Added margin
    html += " input[type=number] { padding: 8px; width: 60px; margin-right: 5px;}";
    html += " .status { background-color: #f0f0f0; padding: 15px; border-radius: 5px; margin-bottom: 20px;}";
    html += " .manual { color: orange; font-weight: bold;} .auto { color: green; font-weight: bold;}";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>ESP32 Control Panel</h1>";

    html += "<div class='status'>";
    html += "<p>LED State: <strong>";
    html += (ledState == HIGH) ? "ON" : "OFF";
    html += "</strong> | Mode: <strong class='";
    html += (ledControlMode == CONTROL_MODE_MANUAL) ? "manual" : "auto";
    html += "'>";
    html += (ledControlMode == CONTROL_MODE_MANUAL) ? "Manual Override" : "Auto";
    html += "</strong></p>";
    html += "<p>Servo Angle: <strong>" + String(servoAngle) + "° (" + current_servo_status + ")";
    html += "</strong> | Mode: <strong class='";
    html += (servoControlMode == CONTROL_MODE_MANUAL) ? "manual" : "auto";
    html += "'>";
    html += (servoControlMode == CONTROL_MODE_MANUAL) ? "Manual Override" : "Auto";
    html += "</strong></p>";
    html += "</div>";

    html += "<h2>Commands:</h2>"; // Changed heading slightly
    html += "<ul>";
    html += "<li>LED Manual Control: ";
    html += "<button onclick=\"window.location.href='/led/on'\">Turn ON</button>";
    html += "<button onclick=\"window.location.href='/led/off'\">Turn OFF</button>";
    html += "</li>";
    html += "<li>Servo Manual Control: <form action='/servo' method='get' style='display: inline;'>";
    html += "<input type='number' name='angle' min='0' max='180' value='" + String(servoAngle) + "' required>";
    html += "<input type='submit' value='Set Angle'></form> (0-180)</li>";
    // <<<--- ADDED BUTTON FOR AUTO MODE --- >>>
    html += "<li>Force Auto Mode: <button onclick=\"window.location.href='/mode/auto'\">Return to Auto</button> (Cancels Manual Overrides)</li>";
    html += "</ul>";

    html += "<hr>";
    html += "<p><i>Page refreshes automatically. Manual control lasts ";
    html += String(MANUAL_MODE_DURATION / 1000);
    html += " seconds per command before returning to Auto mode.</i></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// handleLedOn, handleLedOff, handleServo remain the same
void handleLedOn() {
    Serial.println("HTTP Command received: /led/on");
    applyLedState(HIGH, true);
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleLedOff() {
    Serial.println("HTTP Command received: /led/off");
    applyLedState(LOW, true);
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleServo() {
    String angleStr = "";
    int requestedAngle = -1;

    if (server.hasArg("angle")) {
        angleStr = server.arg("angle");
        if (isNumeric(angleStr)) {
            requestedAngle = angleStr.toInt();
        }
    }

    if (requestedAngle >= 0 && requestedAngle <= 180) {
        Serial.print("HTTP Command received: /servo?angle=");
        Serial.println(requestedAngle);
        applyServoAngle(requestedAngle, true);
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    } else {
        Serial.println("HTTP Command error: Invalid or missing 'angle' parameter for /servo");
        String errorHtml = "<html><head><title>Error</title></head><body>";
        errorHtml += "<h1>400 Bad Request</h1>";
        errorHtml += "<p>Invalid or missing 'angle' parameter. Please provide a number between 0 and 180.</p>";
        errorHtml += "<p>Example: <code>/servo?angle=90</code></p>";
        errorHtml += "<p><a href='/'>Back to Control Panel</a></p>";
        errorHtml += "</body></html>";
        server.send(400, "text/html", errorHtml);
    }
}

// <<<--- ADDED: Handler function for the new button --- >>>
void handleForceAutoMode() {
    Serial.println("HTTP Command received: /mode/auto");
    bool modeChanged = false;

    // Check and set LED mode
    if (ledControlMode != CONTROL_MODE_AUTO) {
        Serial.println("Forcing LED to Auto mode.");
        ledControlMode = CONTROL_MODE_AUTO;
        // Update lastPirState immediately when forcing auto mode
        lastPirState = digitalRead(PIR_PIN);
        modeChanged = true;
    }

    // Check and set Servo mode
    if (servoControlMode != CONTROL_MODE_AUTO) {
        Serial.println("Forcing Servo to Auto mode.");
        servoControlMode = CONTROL_MODE_AUTO;
        modeChanged = true;
    }

    // Publish status if any mode actually changed
    if (modeChanged) {
        publishMqttStatus();
    }

    // Redirect back to the root page
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}


// handleNotFound remains the same
void handleNotFound() {
    String message = "404 Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
    Serial.print("404 Not Found: "); Serial.println(server.uri());
}

// --- Setup Function ---
void setup() {
    Serial.begin(115200);
    while (!Serial);
    delay(100);

    // Pin Initialization
    pinMode(PIR_PIN, INPUT);
    pinMode(PHOTO_DO_PIN, INPUT);
    pinMode(PHOTO_AO_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, ledState);
    lastPirState = digitalRead(PIR_PIN);

    // Sensor Initialization
    dht.begin();
    delay(100);

    // Servo Initialization
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    myServo.setPeriodHertz(50);
    myServo.attach(SERVO_PIN, 500, 2400);
    myServo.write(servoAngle);

    Serial.println("\n\n--- ESP32 Sensor/Actuator System Initializing ---");

    // Network Initialization
    setup_wifi();

    if (WiFi.status() == WL_CONNECTED) {
        client.setServer(mqtt_server, mqtt_port);
        client.setCallback(mqttCallback);
    } else {
         Serial.println("WARNING: MQTT client not configured (WiFi not connected).");
    }

    // Web Server Setup
    if (WiFi.status() == WL_CONNECTED) {
        server.on("/", HTTP_GET, handleRoot);
        server.on("/led/on", HTTP_GET, handleLedOn);
        server.on("/led/off", HTTP_GET, handleLedOff);
        server.on("/servo", HTTP_GET, handleServo);
        server.on("/mode/auto", HTTP_GET, handleForceAutoMode); // <<<--- ADDED: Route for new handler
        server.onNotFound(handleNotFound);
        server.begin();
        Serial.println("HTTP server started. Access at http://" + WiFi.localIP().toString() + "/");
    } else {
        Serial.println("WARNING: HTTP server NOT started (WiFi not connected).");
    }

    Serial.println("---------------- System Initialized -----------------");
    Serial.println("Control Modes:");
    Serial.println(" - Auto: Controlled by PIR sensor (LED) / MQTT commands (LED/Servo).");
    Serial.println(" - Manual: Controlled by Web UI (LED/Servo) / Serial commands (LED only)");
    Serial.println("           (overrides Auto for " + String(MANUAL_MODE_DURATION/1000) + "s).");
    Serial.println("Serial Commands: ON, OFF (for LED)");
    Serial.println("----------------------------------------------------");
}

// --- Main Loop ---
void loop() {
    unsigned long now = millis();

    // --- WiFi Connection Maintenance ---
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastWifiRetry = 0;
        if (now - lastWifiRetry > 10000) {
             Serial.println("WiFi disconnected. Attempting reconnect...");
             setup_wifi();
             if (WiFi.status() == WL_CONNECTED) {
                 Serial.println("WiFi Reconnected. Reconfiguring MQTT...");
                 client.setServer(mqtt_server, mqtt_port);
                 client.setCallback(mqttCallback);
                 Serial.println("WiFi IP: " + WiFi.localIP().toString());
             }
             lastWifiRetry = now;
        }
    } else { // --- Only run network-dependent tasks if WiFi is connected ---
        if (!client.connected()) {
            static unsigned long lastMqttRetry = 0;
            if (now - lastMqttRetry > 5000) {
                reconnect_mqtt();
                lastMqttRetry = now;
            }
        } else {
            client.loop();
        }
        server.handleClient();
    } // --- End WiFi Connected Check ---


    // --- Check for Manual Mode Timeouts ---
    // LED Timeout
    if (ledControlMode == CONTROL_MODE_MANUAL && (now - lastLedManualCommandTime) > MANUAL_MODE_DURATION) {
        Serial.println("LED: Manual Mode Timed Out. Returning to Auto.");
        ledControlMode = CONTROL_MODE_AUTO;
        lastPirState = digitalRead(PIR_PIN); // Update PIR state upon returning to Auto
    }
     // Servo Timeout
     if (servoControlMode == CONTROL_MODE_MANUAL && (now - lastServoManualCommandTime) > MANUAL_MODE_DURATION) {
        Serial.println("Servo: Manual Mode Timed Out. Returning to Auto.");
        servoControlMode = CONTROL_MODE_AUTO; // Return Servo to Auto
    }


    // --- Serial Command Handling (LED Only) ---
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toUpperCase();

        if (command.equals("ON")) {
            Serial.println("Serial Cmd: LED ON");
            applyLedState(HIGH, true);
        } else if (command.equals("OFF")) {
            Serial.println("Serial Cmd: LED OFF");
            applyLedState(LOW, true);
        }
        else if (command.length() > 0) {
            Serial.println("Serial Cmd Error: Invalid command! Use 'ON' or 'OFF' (for LED).");
        }
        while(Serial.available() > 0) { Serial.read(); }
    }

    // --- Timed MQTT Status Publishing ---
    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      if (now - lastMqttPublish > MQTT_PUBLISH_INTERVAL) {
          lastMqttPublish = now;
          publishMqttStatus();
      }
    }


    // --- PIR Sensor Logic for LED (Only runs if LED is in AUTO mode) ---
    if (ledControlMode == CONTROL_MODE_AUTO) {
        int currentPirState = digitalRead(PIR_PIN);
        bool motionJustStarted = (currentPirState == HIGH && lastPirState == LOW);

        if (currentPirState == HIGH) {
            lastMotionTime = now;
            if (motionJustStarted && !ledState) {
                Serial.println("PIR Motion Started (Auto Mode)! Turning LED ON.");
                applyLedState(HIGH, false);
            }
        } else {
            if (ledState && (now - lastMotionTime) > PIR_MOTION_TIMEOUT) {
                 if (lastMotionTime != 0) {
                      Serial.println("PIR No Motion Timeout (Auto Mode). Turning LED OFF.");
                      applyLedState(LOW, false);
                 }
            }
        }
        lastPirState = currentPirState;
    } // --- End of AUTO mode check for PIR ---


    delay(10);
}

// --- isNumeric Function ---
bool isNumeric(String str) {
    if (str.length() == 0) return false;
    unsigned int len = str.length();
    bool hasDigit = false;

    for (unsigned int i = 0; i < len; i++) {
        if (i == 0 && str.charAt(i) == '-') {
            if (len == 1) return false;
            continue;
        }
        if (isDigit(str.charAt(i))) {
            hasDigit = true;
        } else {
            return false;
        }
    }
    return hasDigit;
}