# Smart Home Control System

## Project Overview
This project implements a smart home control system that automates home appliances (curtains and lights) based on environmental conditions and user habits. The system integrates IoT sensors with real-time cloud analytics and AI-driven decision-making. It is designed as part of the COMP4436-25-P2 project, focusing on optimizing energy usage and enhancing user comfort through automation.

The system architecture consists of three main components:
- **Edge**: ESP32 microcontroller collecting sensor data and controlling actuators.
- **Cloud**: MQTT broker (Mosquitto), data storage (InfluxDB), AI processing, and integration with the Hong Kong Observatory API.
- **Front End**: Web-based interface with RESTful API for manual control and data visualization (via Grafana).

## System Architecture
The architecture is divided into three layers:

### 1. Edge
- **MCU Used**: ESP32
- **Input**:
  - Temperature/Humidity Sensor: DHT11
  - Light Intensity Sensor: 
  - PIR Sensor: HC-SR501
- **Output Control**:
  - Servo for Curtain Control
  - LED for Light On/Off
- **Functionality**: Collects real-time sensor data (temperature, humidity, light intensity, motion) and sends it to the cloud via MQTT. Executes commands received from the cloud to control the curtain and LED.

### 2. Cloud
- **MQTT Broker**: Mosquitto
  - Handles MQTT communication between Edge and Cloud.
  - Transmits commands and receives sensor data.
- **Node-Red**:
  - Read the data from MQTT and save it into the InfulxDB.
- **InfluxDB**:
  - Stores real-time sensor data (temperature, humidity, light, motion).
  - Maintains historical data (curtain and LED state, user preferences collected from the front end).
- **Hong Kong Observatory API**:
  - Provides sunrise and sunset times for intelligent automation.
- **Artificial Intelligence**:
  - Uses reinforcement learning to control curtain and light based on occupancy data.
  - Makes predictions to optimize appliance settings.

### 3. Front End
- **Buttons for Controlling Curtain and LED**: Allows manual override of automation.
- **Current Curtain and LED Status**: Displays real-time status of appliances.
- **Current Sensor Data**: Shows live sensor readings.
- **Historical Data (Pattern) / Data Visualization (Grafana)**: Visualizes long-term trends and patterns.
- **HKO Information**: Displays sunrise and sunset data and other data from the Hong Kong Observatory.
- **AI Suggestion**: Provides recommendations based on AI predictions.

## Features
- Real-time sensor data collection and appliance control.
- Automated curtain and light adjustment based on environmental conditions and time of day.
- Historical data storage and visualization for trend analysis.
- AI-driven predictions to optimize energy usage and user comfort.
- Manual control via a web-based interface.

## Prerequisites
- **Hardware**:
  - ESP32 microcontroller
  - DHT11 temperature/humidity sensor
  - HC-SR501 PIR motion sensor
  - Light intensity sensor (e.g., LDR)
  - Servo motor
  - LED
- **Software**:
  - Arduino IDE (for ESP32 programming)
  - Python 3.12
  - Mosquitto MQTT Broker
  - InfluxDB
  - Grafana
  - HTML,CSS, JS
- **Libraries**:
  - Arduino: WiFi, WebServer, PubSubClient, ArduinoJson, DHT, ESP32Servo
  - Python: pandas, joblib, time, requests, os, datetime, flask, influxdb, numpy, traceback

## Installation and Setup

### 1. Edge Setup
- **Install Arduino IDE** and add ESP32 board support.
- **Connect Hardware**:
  - Connect PIR sensor to GPIO 25.
  - Connect DHT sensor to GPIO 33.
  - Connect LDR (Photoresistor) to GPIO 34 for digital read and GPIO 32 for analog read.
  - Attach servo to GPIO 27 and LED to GPIO 26.
- **Upload Code**:
  - Open `esp.ino` in Arduino IDE.
  - Update `ssid`, `password`, and `mqtt_server` with your WiFi and MQTT broker details.
  - Compile and upload to ESP32.

### 2. Cloud Setup
- **Install Mosquitto**:
  - Follow instructions at [Mosquitto Documentation](https://mosquitto.org/download/) to set up an MQTT broker.
- **Set Up NodeRed**:
  - Install NodeRed over at: [NodeRed Download](https://nodered.org/docs/getting-started/local)
  - Import the flow of [nodeRedFLOW.json](https://github.com/wilson-cheng1110/Comp4436_GP/blob/main/nodeRedFLOW.json)
- **Set Up InfluxDB**:
  - Install InfluxDB and create a bucket named `smart_home`.
  - Update `INFLUXDB_URL`, `INFLUXDB_TOKEN`, and `INFLUXDB_ORG` in `cloud/cloud.py`.
- **Run Cloud Script**:
  - Install required Python packages: `pip install paho-mqtt influxdb-client requests`.
  - Update `MQTT_BROKER`, `HKO_API_KEY`, and other placeholders in `cloud/cloud.py`.
  - Run the script: `python cloud/cloud.py`.
- **AI Integration**:
  - Implement a reinforcement learning model (e.g., using `scikit-learn` or TensorFlow) and integrate it into the cloud script.

### 3. Front End and Python Backend Setup
- **Install Dependencies**:
  - Python 3.12.4 is used.
  - pip install any libraries that you found missing.
- **Run RESTful API**:
  - Install Flask: `pip install flask`.
  - Run `front_end/api.py` with updated MQTT broker IP.
- **Access Dashboard**:
  - Run `frontEnd.py` in python to host a dashboard server.
  - Access it using localhost:5000
  - Configure Grafana to connect to InfluxDB for historical data visualization.

## Usage
1. Power on the ESP32 to start sending sensor data to the MQTT broker.
2. The cloud script processes data, stores it in InfluxDB, and sends control commands based on AI logic or HKO data.
3. Access the front-end dashboard to view real-time data, historical trends, and manually control curtains and lights.
4. Monitor AI suggestions for optimizing settings.

## Contributing
Feel free to fork this repository and submit pull requests for enhancements. Contributions to improve AI models, add new features, or optimize code are welcome.

## License
This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.

## Acknowledgments
- Hong Kong Observatory for providing weather data.
