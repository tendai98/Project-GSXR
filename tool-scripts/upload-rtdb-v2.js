// upload_tracks_rtdb_time_string_keys.js
// Upload sensor_tracks.json to Firebase Realtime Database
// using time (with decimal) as the key (string, sanitized).

const fs = require("fs");
const path = require("path");
const admin = require("firebase-admin");

// ---------- CONFIG ----------

// Service account JSON from Firebase console
const serviceAccount = require("./serviceAccountKey.json");

// IMPORTANT: replace <your-project-id> with your actual project id
admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: "https://project-gxxr-default-rtdb.firebaseio.com",
});

// Root path in RTDB where data goes
// Final structure: /sensorTracks/<node>/<timeKey>
const ROOT_PATH = "/sensorTracks_Sat_15_11_2025";

// How many records to send per update() call
const BATCH_SIZE = 200;

// Default JSON file name if not passed on CLI
const DEFAULT_DATA_FILE = "sensor_tracks.json";

const db = admin.database();

// ---------- HELPERS ----------

// Build a Firebase-safe key from a record's `time`.
//
// 1. Take rec.time (epoch seconds, float).
// 2. Convert to string -> "1763096111.1761165".
// 3. Replace "." with "_" because "." is not allowed in RTDB keys.
//
// Result: "1763096111_1761165"
function makeKeyFromTime(rec) {
  let t = rec && rec.time;

  if (typeof t !== "number") {
    // Fallback if time is missing; you can tweak this if needed
    t = Date.now() / 1000;
  }

  const timeStr = t.toString(); // preserves decimal part as JS knows it

  // Make it Firebase RTDB key-safe
  const safeKey = timeStr.replace(".", "_");

  return safeKey;
}

// ---------- MAIN UPLOAD LOGIC ----------

async function uploadTracksFromFile(jsonPath) {
  const fullPath = path.resolve(jsonPath);
  console.log(`Loading data from ${fullPath} ...`);

  const raw = fs.readFileSync(fullPath, "utf8");
  const tracks = JSON.parse(raw);

  const rootRef = db.ref(ROOT_PATH);

  for (const [nodeName, records] of Object.entries(tracks)) {
    if (!Array.isArray(records)) {
      console.warn(`Skipping node "${nodeName}" (not an array)`);
      continue;
    }

    console.log(`\nNode "${nodeName}": ${records.length} records`);

    if (records.length === 0) {
      console.log(`  Node "${nodeName}" is empty, skipping.`);
      continue;
    }

    let batch = {};
    let batchCount = 0;
    let uploadedForNode = 0;

    for (const rec of records) {
      const key = makeKeyFromTime(rec);

      // WARNING: if you somehow have two records with the exact same
      // time for the same node, the later one will overwrite the earlier.
      // If that becomes an issue, we can append a small suffix here.

      const payload = {
        node: nodeName,
        ...rec,
      };

      const pathKey = `${nodeName}/${key}`;
      batch[pathKey] = payload;
      batchCount++;
      uploadedForNode++;

      if (batchCount >= BATCH_SIZE) {
        console.log(
          `  Node ${nodeName}: uploading batch of ${batchCount} records (total so far: ${uploadedForNode})`
        );
        await rootRef.update(batch);
        batch = {};
        batchCount = 0;
      }
    }

    // flush final partial batch
    if (batchCount > 0) {
      console.log(
        `  Node ${nodeName}: uploading final batch of ${batchCount} records (total: ${uploadedForNode})`
      );
      await rootRef.update(batch);
    }

    console.log(
      `  ✅ Finished node "${nodeName}" (${uploadedForNode} records uploaded)`
    );
  }

  console.log("\n✅ All nodes uploaded.");
}

// ---------- ENTRYPOINT ----------

(async () => {
  try {
    const jsonPath = process.argv[2] || DEFAULT_DATA_FILE;
    await uploadTracksFromFile(jsonPath);
    process.exit(0);
  } catch (err) {
    console.error("❌ Upload failed:", err);
    process.exit(1);
  }
})();
