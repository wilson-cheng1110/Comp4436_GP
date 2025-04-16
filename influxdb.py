from influxdb import InfluxDBClient

# Connect to InfluxDB
client = InfluxDBClient(host='localhost', port=8086)


# Switch to the database
client.switch_database('sensor_data')


# Read data
result = client.query('SELECT * FROM sensor_data')
print("Result: {0}".format(result))

# # Delete data with condition
# client.query('DELETE FROM temperature WHERE "location" = \'lab\'')

# # Verify deletion
# result_after_deletion = client.query('SELECT * FROM temperature')
# print("Result after deletion: {0}".format(result_after_deletion))

# # Drop the database
# client.drop_database('example_db')

# Close the connection
client.close()
