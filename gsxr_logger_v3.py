import socket
import json
import time
import threading

# ==== CONFIG ======================================================

UDP_IP = "0.0.0.0"

# Incoming sensor ports (name, port)
SENSORS = [
    ("front_imu",        6666),
    ("rear_brake_gps",   5555),
    ("front_tyre",       3333),
    ("brakes_imu",       7777),
    ("front_brakes_tps", 4444),
    ("primary_imu",      2222),
    ("rear_tyre",        1111),
]

# Master time grid step (seconds)
FRAME_DT = 0.02   # 20 ms -> 50 Hz; tweak as needed

# Output file: one flat JSON frame per line
JSON_LOG_FILE = "sensor_frames_flat.jsonl"

# Optional live stream of frames to other UDP clients
STREAM_ENABLE   = True            # set True if you want streaming
STREAM_UDP_IP   = "127.0.0.1"
STREAM_UDP_PORT = 9000

# How often to print stats
PRINT_EVERY_FRAMES = 100


# ==== SHARED STATE ===============================================

sensor_names = [name for name, _ in SENSORS]

# Latest sample per sensor: {"packet": dict, "log_ts": float}
latest = {name: None for name in sensor_names}
latest_lock = threading.Lock()

# Counters for debug
packet_counts = {name: 0 for name in sensor_names}
packet_counts_lock = threading.Lock()
frames_logged = 0
frames_logged_lock = threading.Lock()


# ==== FLATTEN HELPER =============================================

def flatten_recursive(prefix: str, obj, out: dict):
    """
    Recursively flatten a nested object into out with key prefix.

    Example:
      prefix="brakes_imu_imu_"
      obj={"ax":1, "ay":2}  -> brakes_imu_imu_ax, brakes_imu_imu_ay
    """
    if isinstance(obj, dict):
        for k, v in obj.items():
            flatten_recursive(prefix + k + "_", v, out)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            flatten_recursive(prefix + str(i) + "_", v, out)
    else:
        key = prefix[:-1]  # strip trailing "_"
        out[key] = obj


def flatten_sensor_entry(sensor_name: str, entry: dict, out: dict):
    """
    Flatten one sensor's latest entry into flat keys in out.
    - sensor_name: e.g. "brakes_imu"
    - entry: {"packet": dict, "log_ts": float}
    """
    if entry is None:
        return

    pkt = dict(entry["packet"])  # copy
    log_ts = entry["log_ts"]

    # record when this packet was received
    out[f"{sensor_name}_log_ts"] = log_ts

    # Optionally ignore pkt["node"] if present, since we know sensor_name
    if "node" in pkt:
        pkt = dict(pkt)
        pkt.pop("node", None)

    # flatten everything in the packet
    flatten_recursive(sensor_name + "_", pkt, out)


# ==== UDP RECEIVER THREADS =======================================

def udp_receiver(name: str, port: int):
    """
    Listen on a UDP port, decode JSON packets, and update latest[name].
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((UDP_IP, port))

    print(f"[{name}] Listening for UDP packets on {UDP_IP}:{port} ...")

    global packet_counts

    while True:
        try:
            data, addr = sock.recvfrom(4096)
            log_ts = time.time()  # master clock timestamp
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

            # Update latest sample for this sensor
            with latest_lock:
                latest[name] = {
                    "packet": packet,
                    "log_ts": log_ts,
                }

            # Update packet count
            with packet_counts_lock:
                packet_counts[name] += 1

        except Exception as e:
            print(f"[{name}] Error: {e}")


# ==== AGGREGATOR (TIME-GRID LOGGER) ==============================

def aggregator():
    """
    Time-grid aggregator:
      - Every FRAME_DT seconds, snapshot latest samples.
      - Build one flat frame with:
          frame_ts, frame_idx, and flattened fields per sensor.
      - Append as one JSON line to JSON_LOG_FILE.
      - (Optionally) stream over UDP.
      - Print stats every PRINT_EVERY_FRAMES frames.
    """
    global frames_logged

    frame_idx = 0
    start = time.time()

    stream_sock = None
    if STREAM_ENABLE:
        stream_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"[AGG] Streaming enabled -> {STREAM_UDP_IP}:{STREAM_UDP_PORT}")

    with open(JSON_LOG_FILE, mode="a", buffering=1) as f:
        print(f"[AGG] Logging flat frames to {JSON_LOG_FILE} (dt={FRAME_DT}s)")

        while True:
            # sleep to next grid time
            target = start + frame_idx * FRAME_DT
            now = time.time()
            if target > now:
                time.sleep(target - now)

            frame_idx += 1
            frame_ts = time.time()

            # snapshot latest under lock
            with latest_lock:
                snap = {name: (entry.copy() if entry is not None else None)
                        for name, entry in latest.items()}

            # build flat frame
            frame = {
                "frame_ts": frame_ts,
                "frame_idx": frame_idx,
            }

            for name in sensor_names:
                entry = snap.get(name)
                flatten_sensor_entry(name, entry, frame)

            # append to file as one JSON object per line
            line = json.dumps(frame)
            f.write(line + "\n")

            # optional streaming
            if STREAM_ENABLE and stream_sock is not None:
                try:
                    stream_sock.sendto(line.encode("utf-8"),
                                       (STREAM_UDP_IP, STREAM_UDP_PORT))
                except Exception as e:
                    print(f"[AGG] Stream error: {e}")

            # update frame count + print stats periodically
            with frames_logged_lock:
                frames_logged += 1
                fl = frames_logged

            if fl % PRINT_EVERY_FRAMES == 0:
                with packet_counts_lock:
                    counts_snapshot = dict(packet_counts)
                print(f"[AGG] Frames logged: {fl} | "
                      f"Packets per sensor: {counts_snapshot}")


# ==== MAIN =======================================================

def main():
    # start aggregator
    agg_thread = threading.Thread(target=aggregator, daemon=True)
    agg_thread.start()

    # start receivers
    threads = []
    for name, port in SENSORS:
        t = threading.Thread(target=udp_receiver, args=(name, port), daemon=True)
        t.start()
        threads.append(t)

    print("All listeners started. Common time-grid flat logging active. Ctrl+C to stop.")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping logger (threads are daemonic, process exits).")


if __name__ == "__main__":
    main()
