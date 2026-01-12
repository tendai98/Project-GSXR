import socket
import json
import csv
import time
import os

# UDP server setup
UDP_IP = "0.0.0.0"   # listen on all interfaces
UDP_PORT = 8888

# CSV file setup
CSV_FILE = "sensor_log.csv"
fieldnames = ["timestamp", "ts", "node", "d6", "tempC", "neutral"]

# Ensure CSV file has header if not exists
file_exists = os.path.isfile(CSV_FILE)
with open(CSV_FILE, mode="a", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    if not file_exists:
        writer.writeheader()

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"Listening for UDP packets on port {UDP_PORT}...")

while True:
    try:
        data, addr = sock.recvfrom(1024)  # buffer size 1024 bytes
        msg = data.decode("utf-8").strip()

        # Parse JSON safely
        try:
            packet = json.loads(msg)
        except json.JSONDecodeError:
            print(f"Invalid JSON from {addr}: {msg}")
            continue

        # Add timestamp
        packet["timestamp"] = int(time.time())

        # Write to CSV immediately
        with open(CSV_FILE, mode="a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writerow(packet)

        # Print to console (non-blocking)
        print(packet)

    except Exception as e:
        print(f"Error: {e}")
