import socket
import json
import csv
import time
import os
import threading
from queue import Queue

# ==== CONFIG ====

UDP_IP = "0.0.0.0"

# name here is the logical node/source for that port
SENSORS = [
    ("front_imu",        6666),
    ("rear_brake_gps",   5555),
    ("front_tyre",       3333),
    ("brakes_imu",       7777),
    ("front_brakes_tps", 4444),
    ("primary_imu",	 2222),
    ("rear_tyre",        1111),
]

CSV_FILE = "sensor_tracks_log.csv"

# Wide CSV: each origin has its own "track" columns
FIELDNAMES = [
    "log_ts",      # logging time (PC epoch seconds)
    "src_node",    # which node this row came from

    # front_imu.py: ["ts", "node", "imu", "sonar","timestamp"]
    "front_imu_ts",
    "front_imu_imu",
    "front_imu_sonar",

    # rear_brake_gps.py: ["timestamp", "node", "S1", "S2", "gps", "rear_ss"]
    "rear_brake_S1",
    "rear_brake_S2",
    "rear_brake_gps",
    "rear_brake_ss",

    # front_tyre.py: ["timestamp", "node", "S1", "S2", "S3"]
    "front_tyre_S1",
    "front_tyre_S2",
    "front_tyre_S3",

    # brakes_imu.py: ["timestamp","ts","in","imu","node"]
    "brakes_imu_ts",
    "brakes_imu_in",
    "brakes_imu_imu",

    # front-brakes-tps.py: ["timestamp", "node", "S1", "S2", "tps"]
    "front_brakes_tps_S1",
    "front_brakes_tps_S2",
    "front_brakes_tps_tps",

    # rear_tyre.py: ["timestamp", "node", "S1", "S2", "S3"]
    "rear_tyre_S1",
    "rear_tyre_S2",
    "rear_tyre_S3",

   # primary imu
   "pIMU_A",
   "pIMU_B"
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
    Listen on a UDP port, decode JSON packets, push (source, log_ts, packet) into queue.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((UDP_IP, port))

    print(f"[{name}] Listening for UDP packets on {UDP_IP}:{port} ...")

    while True:
        try:
            data, addr = sock.recvfrom(4096)
            log_ts = time.time()
            text = data.decode("utf-8").strip()
            if not text:
                continue

            try:
                packet = json.loads(text)
            except json.JSONDecodeError:
                print(f"[{name}] JSON decode error, raw: {text!r}")
                continue

            if not isinstance(packet, dict):
                packet = {"raw": packet}

            # Push “raw” packet plus source and PC log timestamp
            queue.put((name, log_ts, packet))

        except Exception as e:
            print(f"[{name}] Error: {e}")


# ==== PER-NODE UNPACKING ====

def build_row_from_packet(src_node: str, log_ts: float, packet: dict) -> dict:
    """
    Take a raw packet and expand it into a wide CSV row with per-node tracks.
    Only the columns for that src_node will be filled; others stay blank.
    """
    row = {key: "" for key in FIELDNAMES}
    row["log_ts"] = log_ts
    row["src_node"] = src_node

    if src_node == "front_imu":
        # packet keys: ts, imu, sonar, node, (maybe timestamp from device later)
        row["front_imu_ts"] = packet.get("ts", "")
        row["front_imu_imu"] = packet.get("imu", "")
        row["front_imu_sonar"] = packet.get("sonar", "")

    elif src_node == "rear_brake_gps":
        # packet keys: S1, S2, gps, rear_ss, node, timestamp (device/PC)
        row["rear_brake_S1"] = packet.get("S1", "")
        row["rear_brake_S2"] = packet.get("S2", "")
        row["rear_brake_gps"] = packet.get("gps", "")
        row["rear_brake_ss"] = packet.get("rear_ss", "")

    elif src_node == "front_tyre":
        # packet keys: S1, S2, S3, node, timestamp
        row["front_tyre_S1"] = packet.get("S1", "")
        row["front_tyre_S2"] = packet.get("S2", "")
        row["front_tyre_S3"] = packet.get("S3", "")

    elif src_node == "brakes_imu":
        # packet keys: ts, in, imu, node, timestamp
        row["brakes_imu_ts"]  = packet.get("ts", "")
        row["brakes_imu_in"]  = packet.get("in", "")
        row["brakes_imu_imu"] = packet.get("imu", "")

    elif src_node == "front_brakes_tps":
        # packet keys: S1, S2, tps, node, timestamp
        row["front_brakes_tps_S1"]  = packet.get("S1", "")
        row["front_brakes_tps_S2"]  = packet.get("S2", "")
        row["front_brakes_tps_tps"] = packet.get("tps", "")

    elif src_node == "rear_tyre":
        # packet keys: S1, S2, S3, node, timestamp
        row["rear_tyre_S1"] = packet.get("S1", "")
        row["rear_tyre_S2"] = packet.get("S2", "")
        row["rear_tyre_S3"] = packet.get("S3", "")

    elif src_node == "primary_imu":
        row["pIMU_A"] = packet.get("mpuA", "")
        row["pIMU_B"] = packet.get("mpuB", "")

    # If you later add more nodes/ports, add new elif branches here.

    return row


# ==== WRITER THREAD ====

def csv_writer(queue: Queue):
    """
    Dedicated CSV writer that drains the queue and writes per-node track rows.
    """
    ensure_csv_header()

    with open(CSV_FILE, mode="a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)

        while True:
            item = queue.get()  # blocking
            if item is None:
                break  # sentinel, if you ever want a clean shutdown

            try:
                src_node, log_ts, packet = item
                row = build_row_from_packet(src_node, log_ts, packet)
                writer.writerow(row)
                f.flush()
               # print(f"[LOGGER] {src_node} -> {row}")
            except Exception as e:
                print(f"[LOGGER] Error writing row: {e}")


# ==== MAIN ====

def main():
    q = Queue()

    # Writer thread
    writer_thread = threading.Thread(target=csv_writer, args=(q,), daemon=True)
    writer_thread.start()

    # One receiver thread per node/port
    threads = []
    for name, port in SENSORS:
        t = threading.Thread(target=udp_receiver, args=(name, port, q), daemon=True)
        t.start()
        threads.append(t)

    print("All listeners started. Logging per-node tracks. Ctrl+C to stop.")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping logger...")
        # Optional clean shutdown:
        q.put(None)
        writer_thread.join()


if __name__ == "__main__":
    main()
