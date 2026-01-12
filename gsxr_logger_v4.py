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
FRAME_DT = 0.02   # 20 ms -> 50 Hz; adjust as needed

# Output file: one flat JSON frame per line
JSON_LOG_FILE = "sensor_frames_flat.jsonl"

# Streaming server
STREAM_ENABLE       = True
STREAM_CONTROL_PORT = 9100         # bound server port for SUB/UNSUB + streaming
MAX_STREAM_CLIENTS  = 5            # cap concurrent clients

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

# Streaming clients: set of (ip, port)
stream_clients = set()
stream_clients_lock = threading.Lock()

# Single UDP socket bound to STREAM_CONTROL_PORT for both control + streaming
stream_sock = None
stream_sock_lock = threading.Lock()


# ==== FLATTEN HELPERS ============================================

def flatten_recursive(prefix: str, obj, out: dict):
    """
    Recursively flatten a nested object into 'out' using 'prefix' for keys.
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
    Flatten one sensor's latest entry into flat keys in 'out'.
      sensor_name: e.g. "brakes_imu"
      entry: {"packet": dict, "log_ts": float}
    """
    if entry is None:
        return

    pkt = dict(entry["packet"])  # copy
    log_ts = entry["log_ts"]

    # record when this packet was received (per-sensor timestamp)
    out[f"{sensor_name}_log_ts"] = log_ts

    # optionally drop 'node' field if present
    if "node" in pkt:
        pkt = dict(pkt)
        pkt.pop("node", None)

    flatten_recursive(sensor_name + "_", pkt, out)


# ==== UDP RECEIVER THREADS (SENSORS IN) ==========================

def udp_receiver(name: str, port: int):
    """
    Listen on a UDP port, decode JSON packets, and update latest[name].
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((UDP_IP, port))

    print(f"[{name}] Listening for UDP packets on {UDP_IP}:{port} ...")

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


# ==== AGGREGATOR (TIME-GRID LOGGER + STREAM OUT) =================

def aggregator():
    """
    Time-grid aggregator:
      - Every FRAME_DT seconds, snapshot latest samples.
      - Build one flat frame with:
          frame_ts, frame_idx, and flattened fields per sensor.
      - Append as one JSON line to JSON_LOG_FILE.
      - Send that same JSON line via the *same socket bound to 9100*
        to all subscribed clients.
      - Print stats every PRINT_EVERY_FRAMES frames.
    """
    global frames_logged, stream_sock

    frame_idx = 0
    start = time.time()

    with open(JSON_LOG_FILE, mode="a", buffering=1) as f:
        print(f"[AGG] Logging flat frames to {JSON_LOG_FILE} (dt={FRAME_DT}s)")

        while True:
            # Sleep until the next grid tick
            target = start + frame_idx * FRAME_DT
            now = time.time()
            if target > now:
                time.sleep(target - now)

            frame_idx += 1
            frame_ts = time.time()

            # Snapshot latest under lock
            with latest_lock:
                snap = {name: (entry.copy() if entry is not None else None)
                        for name, entry in latest.items()}

            # Build flat frame
            frame = {
                "frame_ts": frame_ts,
                "frame_idx": frame_idx,
            }

            for name in sensor_names:
                entry = snap.get(name)
                flatten_sensor_entry(name, entry, frame)

            # Serialize once
            line = json.dumps(frame)

            # Append to log file
            f.write(line + "\n")

            # Stream to all subscribed clients from the *bound* socket
            if STREAM_ENABLE and stream_sock is not None:
                with stream_clients_lock:
                    targets = list(stream_clients)

                if targets:
                    with stream_sock_lock:
                        for addr in targets:
                            try:
                                stream_sock.sendto(line.encode("utf-8"), addr)
                            except Exception as e:
                                print(f"[AGG] Stream send error to {addr}: {e}")

            # Update frame count + print stats periodically
            with frames_logged_lock:
                frames_logged += 1
                fl = frames_logged

            if fl % PRINT_EVERY_FRAMES == 0:
                with packet_counts_lock:
                    counts_snapshot = dict(packet_counts)
                with stream_clients_lock:
                    n_clients = len(stream_clients)
                print(
                    f"[AGG] Frames logged: {fl} | "
                    f"Packets per sensor: {counts_snapshot} | "
                    f"Stream clients: {n_clients}"
                )


# ==== STREAM CONTROL SERVER (SUB/UNSUB + SAME SOCKET) ============

def stream_control_server():
    """
    UDP server that listens for SUBSCRIBE / UNSUBSCRIBE commands from clients
    on the *same socket* that aggregator uses to send frames from.

    - Socket is already bound to STREAM_CONTROL_PORT in main().
    - Clients send UDP packets to STREAM_CONTROL_PORT.
    - addr=(ip,port) of the packet is used as the target for streaming.
    - Maximum of MAX_STREAM_CLIENTS clients kept.
    - If a client "moves" (new port or IP), send SUBSCRIBE again from the new
      address; we remove old entries for that IP and add the new one.
    """
    global stream_sock
    assert stream_sock is not None, "stream_sock must be initialized and bound"

    print(f"[STREAM-CTRL] Listening for clients on {UDP_IP}:{STREAM_CONTROL_PORT} ...")

    while True:
        try:
            data, addr = stream_sock.recvfrom(1024)
            msg = data.decode("utf-8").strip().upper()

            with stream_clients_lock:
                if msg.startswith("SUBSCRIBE"):
                    # Remove any old entries for this IP (client changed port)
                    existing = {c for c in stream_clients if c[0] == addr[0]}
                    if existing:
                        stream_clients.difference_update(existing)

                    if len(stream_clients) < MAX_STREAM_CLIENTS:
                        stream_clients.add(addr)
                        print(
                            f"[STREAM-CTRL] SUBSCRIBE from {addr}, "
                            f"clients={len(stream_clients)}/{MAX_STREAM_CLIENTS}"
                        )
                    else:
                        print(
                            f"[STREAM-CTRL] SUBSCRIBE from {addr} but max clients reached"
                        )

                elif msg.startswith("UNSUBSCRIBE"):
                    if addr in stream_clients:
                        stream_clients.discard(addr)
                        print(
                            f"[STREAM-CTRL] UNSUBSCRIBE from {addr}, "
                            f"clients={len(stream_clients)}/{MAX_STREAM_CLIENTS}"
                        )

                else:
                    print(f"[STREAM-CTRL] Unknown cmd '{msg}' from {addr}")

        except Exception as e:
            print(f"[STREAM-CTRL] Error: {e}")


# ==== MAIN =======================================================

def main():
    global stream_sock

    # Create and bind the single streaming/control socket
    if STREAM_ENABLE:
        stream_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        stream_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        stream_sock.bind((UDP_IP, STREAM_CONTROL_PORT))
        print(f"[MAIN] Stream/control socket bound on {UDP_IP}:{STREAM_CONTROL_PORT}")

    # Start aggregator (logs + sends frames using stream_sock)
    agg_thread = threading.Thread(target=aggregator, daemon=True)
    agg_thread.start()

    # Start stream control server (listens on same stream_sock)
    if STREAM_ENABLE:
        ctrl_thread = threading.Thread(target=stream_control_server, daemon=True)
        ctrl_thread.start()

    # Start sensor receivers
    for name, port in SENSORS:
        t = threading.Thread(target=udp_receiver, args=(name, port), daemon=True)
        t.start()

    print("All listeners started. Flat logging + streaming server active. Ctrl+C to stop.")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping logger (threads are daemonic, process will exit).")


if __name__ == "__main__":
    main()
