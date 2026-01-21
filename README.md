# Project-GSXR

A personal R&D repo tied to my Suzuki GSX-R project: data logging, hardware experiments, a custom dashboard, and analysis notebooks.


## What’s inside

- **Python logging tools** to capture telemetry / sensor streams and write them to files for later analysis (example script: `gsxr_logger_v5.py`).
- **Hardware/firmware experiments** under `hardware-code/` (microcontroller-side code and prototypes).
- **Android dashboard project** under `gsxr-dashboard-android-project/GXXR/` (custom Suzuki GSXR Dashboard).
- **Notebooks** under `notebooks/` for exploration, plotting, and analysis.
- **Release artifacts** under `gsxr-dashboard-releases/` (built outputs / packaged versions).


## Getting started

### 1) Clone
```bash
git clone https://github.com/tendai98/Project-GSXR.git
cd Project-GSXR
````

### 2) Python logger setup (recommended: venv)

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
```

### 3) Run the logger

Start with the latest logger script:

```bash
python gsxr_logger_v5.py
```

## Android dashboard

The Android project lives at:

* `gsxr-dashboard-android-project/GXXR/`

Typical workflow:

1. Open the project in Android Studio
2. Let Gradle sync
3. Run on a device

the dashboard expects live data:

*  the transport (Wi-Fi)

Prebuilt releases (if included) are stored in:

* `gsxr-dashboard-releases/` ([GitHub][2])




