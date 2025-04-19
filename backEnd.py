import pandas as pd
import joblib
import time
import requests
import os
from datetime import datetime, timezone
from flask import Flask, render_template, jsonify, request, abort
from influxdb import InfluxDBClient
from influxdb.exceptions import InfluxDBClientError, InfluxDBServerError
import numpy as np
import traceback

# Assuming ML prediction functions are defined correctly (copied from previous script)
from sklearn.preprocessing import LabelEncoder # Need this even if loading
# You might need other sklearn imports if predict_states uses them implicitly
# but ideally, the loaded pipeline handles preprocessing

# --- Configuration ---
# InfluxDB Config
INFLUXDB_HOST = 'localhost'
INFLUXDB_PORT = 8086
DATABASE_NAME = 'sensor_data'
MEASUREMENT_NAME = 'sensor_data'

# --- Device Control Config ---
DEVICE_IP = "172.20.10.3" # !!! SET YOUR DEVICE IP HERE !!!
REQUEST_TIMEOUT_SECONDS = 5 # Timeout for HTTP requests to the device

# Model & Data Config (Ensure these paths are correct relative to app.py)
LED_MODEL_FILE = 'led_predictor_pipeline_dt.joblib'
CURTAIN_MODEL_FILE = 'curtain_predictor_pipeline_dt.joblib'
LED_ENCODER_FILE = 'led_label_encoder.joblib'
CURTAIN_ENCODER_FILE = 'curtain_label_encoder.joblib'
# PREPROCESSOR_FILE = 'preprocessor.joblib' # Preprocessor is inside the pipeline

# Features expected by the model (MUST match training)
FEATURES = [
    'brightness_raw', 'humidity', 'light_state', 'occupancy',
    'temperature', 'hour', 'dayofweek', 'month', 'dayofyear'
]
NUMERIC_FEATURES = ['brightness_raw', 'humidity', 'temperature', 'hour', 'dayofweek', 'month', 'dayofyear']
CATEGORICAL_FEATURES = ['light_state', 'occupancy']

# HKO API Endpoint (Current Weather Report)
HKO_API_URL = "https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=rhrread&lang=en"

# --- Flask App Setup ---
app = Flask(__name__)

# --- Global Variables for Models (Load Once) ---
led_pipeline = None
curtain_pipeline = None
le_led = None
le_curtain = None

def load_models_globally():
    """Loads models and encoders into global variables."""
    global led_pipeline, curtain_pipeline, le_led, le_curtain
    print("--- Loading Models and Encoders ---")
    try:
        # Check if all required files exist before loading
        required_files = [LED_MODEL_FILE, CURTAIN_MODEL_FILE, LED_ENCODER_FILE, CURTAIN_ENCODER_FILE]
        for f in required_files:
            if not os.path.exists(f):
                print(f"CRITICAL ERROR: Required file not found: {f}. Cannot start.")
                return False # Indicate failure

        led_pipeline = joblib.load(LED_MODEL_FILE)
        curtain_pipeline = joblib.load(CURTAIN_MODEL_FILE)
        le_led = joblib.load(LED_ENCODER_FILE)
        le_curtain = joblib.load(CURTAIN_ENCODER_FILE)
        print("Models and encoders loaded successfully.")
        return True # Indicate success
    except Exception as e:
        print(f"CRITICAL ERROR loading models or encoders: {e}")
        traceback.print_exc()
        return False

# --- Helper Functions ---

def send_device_request(url):
    """Sends a GET request to the device URL."""
    current_time_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    try:
        print(f"[{current_time_str}] Sending request to: {url}")
        response = requests.get(url, timeout=REQUEST_TIMEOUT_SECONDS)
        response.raise_for_status()
        print(f"  [{current_time_str}] Device request successful (Status: {response.status_code})")
        return True, response.status_code, None # Success, status, no error message
    except requests.exceptions.Timeout:
        error_msg = f"Request timed out for {url}"
        print(f"  [{current_time_str}] Error: {error_msg}")
        return False, None, error_msg
    except requests.exceptions.ConnectionError as e:
        error_msg = f"Connection refused or failed for {url}. Is device online? {e}"
        print(f"  [{current_time_str}] Error: {error_msg}")
        return False, None, error_msg
    except requests.exceptions.RequestException as e:
        error_msg = f"Failed to send request to {url}: {e}"
        print(f"  [{current_time_str}] Error: {error_msg}")
        return False, getattr(e.response, 'status_code', None), error_msg # Try to get status code if possible
    except Exception as e:
        error_msg = f"An unexpected error occurred sending request to {url}: {e}"
        print(f"  [{current_time_str}] Error: {error_msg}")
        return False, None, error_msg


def get_latest_sensor_data(client, measurement):
    """Fetches the latest data point from InfluxDB. Returns DataFrame with datetime object or None."""
    current_time_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    try:
        query = f'SELECT * FROM "{measurement}" ORDER BY time DESC LIMIT 1'
        result = client.query(query)
        points = list(result.get_points())
        if not points: return None
        latest_point = points[0]
        df_latest = pd.DataFrame([latest_point])

        if 'time' not in df_latest.columns or pd.isna(df_latest['time'].iloc[0]):
             print(f"[{current_time_str}] Warning: Missing/invalid 'time' in latest data: {latest_point}")
             return None

        # --- IMPORTANT: Convert to datetime object ---
        # Keep it as a datetime object for processing/prediction
        try:
            df_latest['time'] = pd.to_datetime(df_latest['time'], errors='coerce').dt.tz_convert(None) # Convert to naive datetime
            if df_latest['time'].isnull().any():
                 print(f"[{current_time_str}] Warning: Could not parse timestamp: {latest_point.get('time')}")
                 return None
        except Exception as e:
             print(f"[{current_time_str}] Error converting timestamp '{latest_point.get('time')}': {e}")
             return None

        # Convert numeric types, coercing errors
        for col in NUMERIC_FEATURES:
            # Skip time-based features derived later
            if col in df_latest.columns and col not in ['hour', 'dayofweek', 'month', 'dayofyear']:
                df_latest[col] = pd.to_numeric(df_latest[col], errors='coerce')
        return df_latest

    except Exception as e:
        print(f"[{current_time_str}] Error getting latest sensor data: {e}")
        traceback.print_exc()
        return None

def predict_states(data_df):
    """Preprocesses data and uses models to predict LED and Curtain states.
       Expects data_df['time'] to be a datetime object.
    """
    if data_df is None or data_df.empty: return None, None
    if 'time' not in data_df.columns or not isinstance(data_df['time'].iloc[0], pd.Timestamp):
         print(f"Error in predict_states: 'time' column missing or not a datetime object. Type: {type(data_df['time'].iloc[0]) if 'time' in data_df.columns else 'Missing'}")
         return None, None
    if not all([led_pipeline, curtain_pipeline, le_led, le_curtain]): # Check if models loaded
         print("Models not loaded, cannot predict.")
         return None, None

    current_time_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    # Use a copy to prevent modifying the DataFrame passed from /status
    data_df_processed = data_df.copy()
    try:
        # --- Feature Engineering ---
        # Now 'timestamp' will be a datetime object, allowing access to .hour etc.
        timestamp = data_df_processed['time'].iloc[0]
        data_df_processed['hour'] = timestamp.hour
        data_df_processed['dayofweek'] = timestamp.dayofweek
        data_df_processed['month'] = timestamp.month
        data_df_processed['dayofyear'] = timestamp.dayofyear

        # --- Ensure all features exist, add NaNs if not ---
        for feature in FEATURES:
            if feature not in data_df_processed.columns:
                print(f"[{current_time_str}] Warning: Feature '{feature}' missing in data for prediction. Adding as NaN.")
                data_df_processed[feature] = np.nan

        # Select features in the correct order
        X_latest = data_df_processed[FEATURES]

        # --- Handle NaNs before pipeline ---
        # Note: Ensure this imputation strategy aligns with how training data was handled
        # The pipeline *might* have an imputer, but StandardScaler usually requires no NaNs beforehand.
        imputed_flag = False
        for col in NUMERIC_FEATURES:
            if col in X_latest.columns and X_latest[col].isnull().any():
                # Using fillna(0) is simple, but might not be optimal. Consider median/mean.
                print(f"[{current_time_str}] Warning: NaN found in numeric feature '{col}' for prediction. Filling with 0.")
                # Use .loc to avoid SettingWithCopyWarning
                X_latest.loc[:, col] = X_latest[col].fillna(0)
                imputed_flag = True
        for col in CATEGORICAL_FEATURES:
            if col in X_latest.columns and X_latest[col].isnull().any():
                print(f"[{current_time_str}] Warning: NaN found in categorical feature '{col}' for prediction. Filling with 'Unknown'.")
                # Use .loc to avoid SettingWithCopyWarning
                X_latest.loc[:, col] = X_latest[col].fillna('Unknown')
                imputed_flag = True

        # --- Predict ---
        # The pipeline handles the actual preprocessing (scaling, OHE)
        led_pred_encoded = led_pipeline.predict(X_latest)
        curtain_pred_encoded = curtain_pipeline.predict(X_latest)

        led_state_predicted = le_led.inverse_transform(led_pred_encoded)[0]
        curtain_state_predicted = le_curtain.inverse_transform(curtain_pred_encoded)[0]

        return led_state_predicted, curtain_state_predicted

    except AttributeError as e:
        print(f"[{current_time_str}] Error during prediction (AttributeError): {e}")
        print(f"Check if 'time' column was correctly passed as datetime. Timestamp type: {type(timestamp)}")
        traceback.print_exc()
        return None, None
    except Exception as e:
        print(f"[{current_time_str}] Error during prediction: {e}")
        print(f"Data causing issue (first row):\n{X_latest.iloc[[0]].to_string()}")
        traceback.print_exc()
        return None, None

def get_hko_weather():
    """Fetches current weather data from HKO API."""
    try:
        response = requests.get(HKO_API_URL, timeout=REQUEST_TIMEOUT_SECONDS)
        response.raise_for_status()
        return response.json() # Return parsed JSON
    except requests.exceptions.RequestException as e:
        print(f"Error fetching HKO data: {e}")
        return {"error": f"Could not fetch HKO data: {e}"}
    except Exception as e:
        print(f"Unexpected error fetching HKO data: {e}")
        return {"error": "Unexpected error fetching HKO data"}


# --- Flask Routes ---

@app.route('/')
def index():
    """Serves the main HTML page."""
    return render_template('index.html')

@app.route('/control/led/<state>', methods=['POST']) # Use POST for actions
def control_led_route(state):
    """Handles requests to control the LED."""
    if state == 'on':
        url = f"http://{DEVICE_IP}/led/on"
    elif state == 'off':
        url = f"http://{DEVICE_IP}/led/off"
    else:
        return jsonify({"status": "error", "message": "Invalid LED state"}), 400

    success, status_code, error_msg = send_device_request(url)
    if success:
        return jsonify({"status": "success", "message": f"LED command '{state}' sent."})
    else:
        # Return a server error if the device request failed
        return jsonify({"status": "error", "message": f"Failed to control LED: {error_msg}"}), status_code or 500

@app.route('/control/servo', methods=['POST']) # Use POST for actions
def control_servo_route():
    """Handles requests to control the servo."""
    angle = request.args.get('angle', type=int) # Get angle from query param
    if angle is None or not (0 <= angle <= 180):
        return jsonify({"status": "error", "message": "Invalid or missing angle parameter (0-180 required)"}), 400

    url = f"http://{DEVICE_IP}/servo?angle={angle}"
    success, status_code, error_msg = send_device_request(url)

    if success:
        return jsonify({"status": "success", "message": f"Servo command sent (angle={angle})."})
    else:
        return jsonify({"status": "error", "message": f"Failed to control servo: {error_msg}"}), status_code or 500


@app.route('/status')
def get_status():
    """API endpoint to provide all current status information."""
    influx_client = None
    sensor_data_dict = None # Use a different name for the final dict
    ai_led = "N/A"
    ai_curtain = "N/A"
    current_time_utc = datetime.now(timezone.utc)
    current_time_local = datetime.now() # Server's local time

    try:
        # --- Get Sensor Data ---
        influx_client = InfluxDBClient(host=INFLUXDB_HOST, port=INFLUXDB_PORT, timeout=5)
        influx_client.switch_database(DATABASE_NAME)
        # This DataFrame has 'time' as a datetime object
        latest_data_df = get_latest_sensor_data(influx_client, MEASUREMENT_NAME)

        if latest_data_df is not None and not latest_data_df.empty:
             # --- Get AI Prediction ---
             # Pass the DataFrame with the datetime object to the prediction function
             pred_led, pred_curtain = predict_states(latest_data_df) # <<< FIX: Pass DF with datetime
             if pred_led is not None: ai_led = pred_led
             if pred_curtain is not None: ai_curtain = pred_curtain

             # --- Prepare Sensor Data for JSON Response ---
             # Now convert the DataFrame row to a dictionary and format the time as a string
             sensor_data_dict = latest_data_df.iloc[0].to_dict()

             # Format the 'time' field specifically for JSON output
             if 'time' in sensor_data_dict and isinstance(sensor_data_dict['time'], pd.Timestamp):
                 # Make sure it's not NaT before formatting
                 if pd.notna(sensor_data_dict['time']):
                      sensor_data_dict['time'] = sensor_data_dict['time'].strftime('%Y-%m-%d %H:%M:%S')
                 else:
                      sensor_data_dict['time'] = "Invalid Timestamp"
             elif 'time' not in sensor_data_dict:
                 sensor_data_dict['time'] = "Missing Timestamp"

             # Convert numpy types to standard python types for JSON
             for key, value in sensor_data_dict.items():
                  if isinstance(value, (np.integer, np.floating, np.bool_)):
                       sensor_data_dict[key] = value.item()
                  elif pd.isna(value): # Handle NaN specifically
                       sensor_data_dict[key] = None # Represent NaN as null in JSON

        else:
            sensor_data_dict = {"error": "Could not retrieve sensor data"}

    except Exception as e:
        print(f"Error in /status route during InfluxDB/AI processing: {e}")
        traceback.print_exc()
        # Ensure sensor_data_dict has an error message if it failed before assignment
        if sensor_data_dict is None:
             sensor_data_dict = {"error": f"Error processing sensor data: {e}"}
    finally:
        if influx_client:
            try: influx_client.close()
            except: pass # Ignore errors during close

    # --- Get HKO Weather ---
    hko_data = get_hko_weather()

    # --- Prepare Response ---
    status_response = {
        "currentTimeUTC": current_time_utc.strftime('%Y-%m-%d %H:%M:%S %Z'),
        "currentTimeLocal": current_time_local.strftime('%Y-%m-%d %H:%M:%S (%A)'),
        "sensorData": sensor_data_dict, # Use the prepared dictionary
        "aiSuggestion": {
            "led": ai_led,
            "curtain": ai_curtain
        },
        "hkoWeather": hko_data
    }

    return jsonify(status_response)


# --- Load models before starting the app ---
if not load_models_globally():
     print("Exiting due to failure loading models.")
     exit(1)


# --- Run Flask App ---
if __name__ == '__main__':
    # Use host='0.0.0.0' to make it accessible from other devices on the network
    # Use debug=True only for development, NEVER in production
    app.run(host='0.0.0.0', port=5000, debug=True)
