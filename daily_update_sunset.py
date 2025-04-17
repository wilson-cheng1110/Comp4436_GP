import requests
import json
from influxdb import InfluxDBClient
from influxdb.exceptions import InfluxDBClientError, InfluxDBServerError
# Import datetime parts needed
from datetime import datetime, time, timezone, date, timedelta # Added timedelta
import time as time_module # Rename standard time module to avoid conflict

# --- Configuration ---
# API Details
API_URL = "https://data.weather.gov.hk/weatherAPI/opendata/opendata.php"
API_PARAMS = {
    "dataType": "SRS",
    "lang": "en",
    "rformat": "json",
    # year, month, day will be added dynamically
}

# InfluxDB Details
INFLUXDB_HOST = 'localhost'
INFLUXDB_PORT = 8086
# INFLUXDB_USER = 'your_user'      # Uncomment if you use authentication
# INFLUXDB_PASSWORD = 'your_password' # Uncomment if you use authentication
INFLUXDB_DATABASE = 'sensor_data' # Database name
INFLUXDB_MEASUREMENT = 'sensor_data' # Measurement (table) name

# Scheduler Configuration
CHECK_INTERVAL_SECONDS = 60 # How often to check if a new day has started (e.g., 60 seconds)

# --- Functions (fetch_weather_data, format_data_for_influxdb, write_to_influxdb) ---
# (These functions remain the same as in the previous versions.
#  They are omitted here for brevity but should be included in your final script.)
# --- Make sure they handle potential errors gracefully (e.g., return None or False) ---

def fetch_weather_data(url, params):
    """Fetches data from the weather API."""
    try:
        print(f"Attempting to fetch data from: {url} with params: {params}")
        response = requests.get(url, params=params, timeout=15) # Add timeout
        response.raise_for_status()
        print(f"Successfully fetched data from API: {response.url}")
        if not response.text:
            print("Warning: API returned empty response.")
            return None
        return response.json()
    except requests.exceptions.Timeout:
        print(f"Error: Timeout occurred while fetching data from API: {url}")
        return None
    except requests.exceptions.RequestException as e:
        print(f"Error fetching data from API: {e}")
        if hasattr(e, 'response') and e.response is not None:
             print(f"API Response Status Code: {e.response.status_code}")
             print(f"API Response Text: {e.response.text}")
        return None
    except json.JSONDecodeError as e:
        print(f"Error decoding JSON response: {e}")
        status_code = response.status_code if 'response' in locals() and response else "N/A"
        response_text = response.text if 'response' in locals() and response else "N/A"
        print(f"Response status code: {status_code}")
        print(f"Response text was: {response_text}")
        return None
    except Exception as e:
        print(f"An unexpected error occurred during fetch: {e}")
        return None

def format_data_for_influxdb(api_data, requested_date_str):
    """Formats the API JSON data into InfluxDB point format."""
    # Add robust checks at the beginning
    if not isinstance(api_data, dict):
         print(f"Formatting Error: API data is not a valid dictionary or is None. Received: {type(api_data)}")
         return None
    if 'fields' not in api_data or 'data' not in api_data:
        msg = api_data.get('message', 'Unknown API Response Structure')
        print(f"Formatting Error: API data missing 'fields' or 'data'. API message: '{msg}'")
        return None
    if not api_data['data']:
        print(f"Formatting Warning: API returned no data rows for date {requested_date_str} (key 'data' is empty).")
        return None

    try:
        field_names = api_data['fields']
        data_row = None
        # Find the data row matching the requested date
        for row in api_data['data']:
            if row and len(row) > 0 and row[0] == requested_date_str:
                 data_row = row
                 break

        if data_row is None:
             print(f"Formatting Warning: API data found, but no row matched the requested date {requested_date_str}.")
             return None # Don't proceed if the specific date's data isn't present

        raw_data_dict = dict(zip(field_names, data_row))
        print(f"Raw data extracted from API: {raw_data_dict}")

        # Extract fields precisely
        sunrise_str = raw_data_dict.get("RISE")
        transit_str = raw_data_dict.get("TRAN.")
        sunset_str = raw_data_dict.get("SET")

        if not all([sunrise_str, transit_str, sunset_str]):
             print(f"Formatting Error: Missing expected time fields ('RISE', 'TRAN.', 'SET') in data row: {data_row}")
             return None

        # Create timestamp based on the requested date (which should be the current date in this setup)
        try:
            data_date = datetime.strptime(requested_date_str, "%Y-%m-%d")
            point_timestamp = datetime.combine(data_date.date(), time(0, 0), tzinfo=timezone.utc)
        except ValueError as e:
            print(f"Internal Formatting Error: Could not parse derived date string '{requested_date_str}': {e}")
            return None

        influx_point = [{
            "measurement": INFLUXDB_MEASUREMENT,
            "time": point_timestamp.isoformat(),
            "tags": { "source": "weather.gov.hk", "data_type": "sun_rise_set", "requested_date": requested_date_str },
            "fields": { "sunrise": sunrise_str, "solar_transit": transit_str, "sunset": sunset_str }
        }]
        print(f"Formatted data point for InfluxDB: {influx_point}")
        return influx_point

    except Exception as e: # Catch broader errors during formatting
        print(f"An unexpected error occurred during data formatting: {e}")
        print(f"Problematic API data structure might be: {api_data}")
        return None


def write_to_influxdb(points):
    """Writes the formatted data points to InfluxDB."""
    if not points:
        print("No data points provided to write function.")
        return False

    client = None
    try:
        print(f"Connecting to InfluxDB at {INFLUXDB_HOST}:{INFLUXDB_PORT}, database '{INFLUXDB_DATABASE}'")
        client = InfluxDBClient(
            host=INFLUXDB_HOST, port=INFLUXDB_PORT,
            # username=INFLUXDB_USER, password=INFLUXDB_PASSWORD, # Uncomment if needed
            database=INFLUXDB_DATABASE, timeout=10, retries=3 # Add timeout and retries
        )

        # Ensure database exists (consider doing this less frequently if performance matters)
        databases = client.get_list_database()
        if {'name': INFLUXDB_DATABASE} not in databases:
            print(f"Database '{INFLUXDB_DATABASE}' not found. Creating it.")
            client.create_database(INFLUXDB_DATABASE)
        client.switch_database(INFLUXDB_DATABASE) # Ensure we are using the correct one

        print(f"Writing points to InfluxDB: {points}")
        success = client.write_points(points)
        if success:
            print(f"Successfully wrote {len(points)} point(s) to InfluxDB measurement '{INFLUXDB_MEASUREMENT}'.")
            return True
        else:
            # write_points returning False with HTTP usually indicates an issue.
            print("Failed to write points to InfluxDB (write_points returned False). Check InfluxDB logs.")
            return False

    except (InfluxDBClientError, InfluxDBServerError) as e:
        print(f"InfluxDB Error: {e}")
        if hasattr(e, 'content'): print(f"Error details: {e.content}")
        return False
    except requests.exceptions.ConnectionError as e:
         print(f"Error connecting to InfluxDB at {INFLUXDB_HOST}:{INFLUXDB_PORT}: {e}")
         return False
    except Exception as e:
        print(f"An unexpected error occurred during InfluxDB write operation: {e}")
        return False
    finally:
        if client:
            try:
                client.close()
                print("InfluxDB connection closed.")
            except Exception as e:
                print(f"Error closing InfluxDB connection: {e}")


# --- Main Execution Loop ---
if __name__ == "__main__":
    print("--- Starting Continuous Weather Data Ingestion Script ---")
    print(f"--- Will check for new day every {CHECK_INTERVAL_SECONDS} seconds ---")

    last_successful_run_date = None # Keep track of the date of the last successful upload

    while True:
        try:
            # Use UTC for date comparison to avoid timezone issues near midnight
            current_utc_date = datetime.now(timezone.utc).date()

            # Check if it's a new day OR if the script just started (first run)
            if last_successful_run_date is None or current_utc_date > last_successful_run_date:
                print(f"\n[{datetime.now()}] New day detected ({current_utc_date}) or first run. Attempting data fetch and upload...")

                # --- Prepare for API call using the current UTC date ---
                # Note: The API might expect local HK time day, but UTC date check triggers the process daily.
                # We use the date part of the UTC timestamp for consistency in triggering.
                target_date_obj = current_utc_date
                requested_date_str = target_date_obj.strftime('%Y-%m-%d')
                year_str = target_date_obj.strftime('%Y')
                month_str = target_date_obj.strftime('%m')
                day_str = target_date_obj.strftime('%d')

                API_PARAMS['year'] = year_str
                API_PARAMS['month'] = month_str
                API_PARAMS['day'] = day_str

                # --- Execute the fetch, format, write sequence ---
                weather_json = fetch_weather_data(API_URL, API_PARAMS)

                if weather_json:
                    influx_data = format_data_for_influxdb(weather_json, requested_date_str)

                    if influx_data:
                        write_success = write_to_influxdb(influx_data)

                        if write_success:
                            print(f"Data ingestion successful for {requested_date_str}.")
                            # *** IMPORTANT: Update last successful run date ONLY on full success ***
                            last_successful_run_date = current_utc_date
                        else:
                            print(f"Data ingestion failed during write for {requested_date_str}. Will retry on next check.")
                    else:
                        print(f"Failed to format data for {requested_date_str}. Will retry on next check.")
                else:
                    print(f"Failed to fetch data for {requested_date_str}. Will retry on next check.")

            # else: # Optional: uncomment for debugging
            #     print(f".", end="", flush=True) # Print a dot to show the loop is running

            # --- Wait before the next check ---
            time_module.sleep(CHECK_INTERVAL_SECONDS)

        except KeyboardInterrupt:
            print("\nScript interrupted by user. Exiting.")
            break # Exit the while loop
        except Exception as e:
            # Catch any other unexpected errors in the loop to prevent the script from crashing
            print(f"\n!!! An unexpected error occurred in the main loop: {e} !!!")
            print("   Will attempt to continue after a short delay.")
            # Optional: Implement a longer sleep or backoff strategy here if errors are frequent
            time_module.sleep(CHECK_INTERVAL_SECONDS * 2) # Sleep a bit longer after an error
