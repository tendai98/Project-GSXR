import socket
import json
import csv
import time
import os
import threading
from queue import Queue

# ==== CONFIG ====

UDP_IP = "0.0.0.0"

SENSORS = [
    ("front_imu",        6666),
    ("rear_brake_gps",   5555),
    ("front_tyre",       3333),
    ("brakes_imu",       7777),
    ("front_brakes_tps", 4444),
    ("primary_imu",	 2222),
    ("rear_tyre",        1111),
]

CSV_FILE = "sensor_log.csv"

# Union of all fields from your individual scripts
FIELDNAMES = [
    "timestamp",  # unified logging time (epoch)
    "ts",         # any device/packet timestamp
    "node",
    "imu",
    "sonar",
    "S1",
    "S2",
    "S3",
    "gps",
    "rear_ss",
    "in",
    "tps",
]


# ==== CSV SETUP ====

def ensure_csv_header():
    """
    Create CSV file with header if it doesn't exist or is empty.
    """
    needs_header = not os.path.exists(CSV_FILE) or os.path.getsize(CSV_FILE) == 0
    if needs_header:
        with open(CSV_FILE, mode="w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
            writer.writeheader()


# ==== RECEIVER THREAD ====

def udp_receiver(name: str, port: int, queue: Queue):
    """
    Listen on a UDP port, decode JSON packets, push dicts into the queue.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((UDP_IP, port))

    print(f"[{name}] Listening for UDP packets on {UDP_IP}:{port} ...")

    while True:
        try:
            data, addr = sock.recvfrom(4096)
            text = data.decode("utf-8").strip()
            if not text:
                continue

            try:
                packet = json.loads(text)
            except json.JSONDecodeError:
                print(f"[{name}] JSON decode error, raw: {text!r}")
                continue

            if not isinstance(packet, dict):
                # If the payload is not a dict, wrap it
                packet = {"raw": packet}

            # Attach unified logging timestamp if not already included
            packet.setdefault("timestamp", int(time.time()))

            # Make sure any unknown fields don't break CSV writing:
            # DictWriter will ignore keys that are not in FIELDNAMES.
            queue.put(packet)

        except Exception as e:
            print(f"[{name}] Error: {e}")


# ==== WRITER THREAD ====

def csv_writer(queue: Queue):
    """
    Dedicated CSV writer thread that drains the queue and writes rows.
    """
    ensure_csv_header()

    with open(CSV_FILE, mode="a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)

        while True:
            packet = queue.get()  # blocking
            if packet is None:
                # Sentinel to shut down writer cleanly (not strictly needed
                # if you just kill the program with Ctrl+C).
                break

            try:
                writer.writerow(packet)
                f.flush()
                print(f"[LOGGER] wrote row: {packet}")
            except Exception as e:
                print(f"[LOGGER] Error writing row: {e}")


# ==== MAIN ====

def main():
    q = Queue()

    # Start writer thread
    writer_thread = threading.Thread(target=csv_writer, args=(q,), daemon=True)
    writer_thread.start()

    # Start one receiver thread per sensor/port
    threads = []
    for name, port in SENSORS:
        t = threading.Thread(target=udp_receiver, args=(name, port, q), daemon=True)
        t.start()
        threads.append(t)

    print("All listeners started. Press Ctrl+C to stop.")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping logger...")
        # Optionally send sentinel to writer if you want a tidy shutdown:
        # q.put(None)
        # writer_thread.join()
        # (Daemon threads will terminate when the main program exits.)


if __name__ == "__main__":
    main()
