import csv
import ast
import json
import argparse
from typing import Any, Dict, List, Optional


def safe_literal_eval(s: str) -> Optional[Dict[str, Any]]:
    """
    Safely parse strings like "{'ax': 1.0, 'ay': 2.0}" into dicts.
    Returns None if empty or invalid.
    """
    s = (s or "").strip()
    if not s:
        return None
    try:
        return ast.literal_eval(s)
    except (SyntaxError, ValueError):
        return None


def safe_float(s: str) -> Optional[float]:
    s = (s or "").strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def safe_int(s: str) -> Optional[int]:
    s = (s or "").strip()
    if not s:
        return None
    try:
        return int(s)
    except ValueError:
        return None


# --- Row parsers for each sensor node ----------------------------


def parse_primary_imu_row(row: List[str]) -> Dict[str, Any]:
    """
    Parse one CSV row from the primary_imu track.

    Expected layout (conceptually):
      0: time   (epoch seconds, float)
      1: node   ("primary_imu")
      ... many empty fields ...
     -2: IMU sample #1 as dict string
     -1: IMU sample #2 as dict string   (optional – if only one IMU, last one only)

    Each IMU dict looks like:
      {'a': {'x': ..., 'y': ..., 'z': ...},
       'g': {'x': ..., 'y': ..., 'z': ...},
       't': 35.27}

    Returned record is flattened to something like:
      {
        "time": ...,
        "node": "primary_imu",
        "imu1_ax": ..., "imu1_ay": ..., "imu1_az": ...,
        "imu1_gx": ..., "imu1_gy": ..., "imu1_gz": ...,
        "imu1_t":  ...,
        "imu2_ax": ..., ...
      }
    """

    rec: Dict[str, Any] = {}

    # base fields
    rec["time"] = safe_float(row[0]) if len(row) > 0 else None
    rec["node"] = (row[1] or "").strip() if len(row) > 1 else ""

    # Grab the LAST 1–2 non-empty fields as IMU dict strings
    non_empty_indices = [
        i for i, v in enumerate(row) if i >= 2 and str(v).strip() != ""
    ]

    imu_strings: List[Optional[str]] = []

    if len(non_empty_indices) >= 1:
        # at least one IMU dict
        imu_strings.append(row[non_empty_indices[-1]])
    if len(non_empty_indices) >= 2:
        # second one before that
        imu_strings.insert(0, row[non_empty_indices[-2]])

    # Parse and flatten each IMU dict
    for idx, imu_str in enumerate(imu_strings, start=1):
        imu_dict = safe_literal_eval(imu_str)
        if not isinstance(imu_dict, dict):
            continue

        a = imu_dict.get("a") or {}
        g = imu_dict.get("g") or {}

        prefix = f"imu{idx}_"

        rec[prefix + "ax"] = a.get("x")
        rec[prefix + "ay"] = a.get("y")
        rec[prefix + "az"] = a.get("z")

        rec[prefix + "gx"] = g.get("x")
        rec[prefix + "gy"] = g.get("y")
        rec[prefix + "gz"] = g.get("z")

        rec[prefix + "t"] = imu_dict.get("t")

    return rec

def parse_brakes_imu(row: List[str]) -> Dict[str, Any]:
    """
    Example row:
    0: timestamp
    1: 'brakes_imu'
    ...
    12: seq      (e.g. 588156)
    13: "{'d5': 1, 'd6': 1}"
    14: "{'ax': ..., 'ay': ..., ... }" (IMU)
    """
    time_val = safe_float(row[0])
    seq = safe_int(row[12]) if len(row) > 12 else None
    digital = safe_literal_eval(row[13]) if len(row) > 13 else None
    imu = safe_literal_eval(row[14]) if len(row) > 14 else None

    return {
        "time": time_val,
        "seq": seq,
        "digital": digital,  # d5, d6, etc.
        "imu": imu           # ax, ay, az, gx, gy, gz, t
    }


def parse_front_brakes_tps(row: List[str]) -> Dict[str, Any]:
    """
    Example row:
    0: timestamp
    1: 'front_brakes_tps'
    ...
    15: 22.71  (e.g. brake pressure / S1)
    17: 8      (e.g. throttle position / gear / S2)
    """
    time_val = safe_float(row[0])
    val1 = safe_float(row[15]) if len(row) > 15 else None
    val2 = safe_float(row[17]) if len(row) > 17 else None

    return {
        "time": time_val,
        "value1": val1,   # rename to 'brake' or 'S1' if you prefer
        "value2": val2    # rename to 'tps' or 'gear' as needed
    }


def parse_front_tyre(row: List[str]) -> Dict[str, Any]:
    """
    Example row:
    0: timestamp
    1: 'front_tyre'
    ...
    9:  S1  (e.g. pressure at one point)
    10: S2
    11: S3
    """
    time_val = safe_float(row[0])
    s1 = safe_float(row[9]) if len(row) > 9 else None
    s2 = safe_float(row[10]) if len(row) > 10 else None
    s3 = safe_float(row[11]) if len(row) > 11 else None

    return {
        "time": time_val,
        "s1": s1,
        "s2": s2,
        "s3": s3
    }


def parse_rear_tyre(row: List[str]) -> Dict[str, Any]:
    """
    Assuming rear_tyre is laid out like front_tyre:
    0: timestamp
    1: 'rear_tyre'
    ...
    9,10,11: S1,S2,S3
    """
    time_val = safe_float(row[0])
    s1 = safe_float(row[9]) if len(row) > 9 else None
    s2 = safe_float(row[10]) if len(row) > 10 else None
    s3 = safe_float(row[11]) if len(row) > 11 else None

    return {
        "time": time_val,
        "s1": s1,
        "s2": s2,
        "s3": s3
    }


def parse_rear_brake_gps(row: List[str]) -> Dict[str, Any]:
    """
    Example row:
    0: timestamp
    1: 'rear_brake_gps'
    ...
    5: 25.73         (scalar – e.g. brake pressure / S1)
    7: "{'lat': ..., 'lon': ..., 'alt': ..., 'spd': ..., 'sats': ..., 'fix': 1}"
    8: "{'mm': 98.1}" (rear suspension / sonar in mm)
    """
    time_val = safe_float(row[0])
    s1 = safe_float(row[5]) if len(row) > 5 else None
    gps = safe_literal_eval(row[7]) if len(row) > 7 else None
    rear_mm = safe_literal_eval(row[8]) if len(row) > 8 else None

    # rear_mm is a dict like {'mm': 98.1}
    mm_val = None
    if isinstance(rear_mm, dict):
        mm_val = rear_mm.get("mm")

    return {
        "time": time_val,
        "s1": s1,
        "gps": gps,       # lat, lon, alt, spd, sats, fix
        "rear_mm": mm_val
    }


def parse_front_imu(row: List[str]) -> Dict[str, Any]:
    """
    Example row:
    0: timestamp
    1: 'front_imu'
    2: seq (e.g. 588169)
    3: "{'ax': ..., 'ay': ..., ... }"  (IMU)
    4: "{'echo_us': 189, 'mm': 32.4, 'cm': 3.24}" (sonar)
    """
    time_val = safe_float(row[0])
    seq = safe_int(row[2]) if len(row) > 2 else None
    imu = safe_literal_eval(row[3]) if len(row) > 3 else None
    sonar = safe_literal_eval(row[4]) if len(row) > 4 else None

    return {
        "time": time_val,
        "seq": seq,
        "imu": imu,       # ax, ay, az, gx, gy, gz, t
        "sonar": sonar    # echo_us, mm, cm
    }


# Map node name -> parser function
NODE_PARSERS = {
    "brakes_imu": parse_brakes_imu,
    "front_brakes_tps": parse_front_brakes_tps,
    "front_tyre": parse_front_tyre,
    "rear_tyre": parse_rear_tyre,
    "rear_brake_gps": parse_rear_brake_gps,
    "front_imu": parse_front_imu,
    "primary_imu": parse_primary_imu_row
}


def convert_csv_to_json(input_csv: str) -> Dict[str, List[Dict[str, Any]]]:
    """
    Read the mixed CSV and build a JSON object:
      {
        "brakes_imu": [ ... ],
        "front_brakes_tps": [ ... ],
        ...
      }
    """
    tracks: Dict[str, List[Dict[str, Any]]] = {name: [] for name in NODE_PARSERS.keys()}

    with open(input_csv, newline="") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or len(row) < 2:
                continue

            node = (row[1] or "").strip()
            if node not in NODE_PARSERS:
                # Unknown / unused node – skip or log if you want
                continue

            parser = NODE_PARSERS[node]
            record = parser(row)
            tracks[node].append(record)

    return tracks


def main():
    parser = argparse.ArgumentParser(description="Convert sensor CSV log to JSON tracks.")
    parser.add_argument("input_csv", help="Path to input CSV file")
    parser.add_argument("output_json", help="Path to output JSON file")
    args = parser.parse_args()

    tracks = convert_csv_to_json(args.input_csv)

    with open(args.output_json, "w") as out:
        json.dump(tracks, out, indent=2)

    print(f"Wrote JSON tracks to {args.output_json}")


if __name__ == "__main__":
    main()
