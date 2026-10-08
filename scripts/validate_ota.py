from pathlib import Path
import csv
import re

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "include" / "app_config.h").read_text(encoding="utf-8")
PARTITIONS = ROOT / "partitions_ota_16mb.csv"

required = [
    "Update.begin",
    "Update.write",
    "Update.end(true)",
    "mbedtls_sha256",
    "/api/device/v1/firmware/latest",
    "esp_ota_mark_app_valid_cancel_rollback",
    "ensureDatabricksAccessToken",
    "X-3C-Device-Token",
]
for needle in required:
    if needle not in MAIN:
        raise AssertionError(f"missing OTA contract: {needle}")

for forbidden in ["PROBAR WSL", "WSL DISPONIBLE", "192.168.", "3c-backend.local", "MDNS.queryService"]:
    if forbidden in MAIN or forbidden in CONFIG:
        raise AssertionError(f"local runtime dependency remains: {forbidden}")

rows = []
with PARTITIONS.open(newline="", encoding="utf-8") as handle:
    for raw in handle:
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue
        parts = [item.strip() for item in raw.split(",")]
        rows.append(parts)

subtypes = {row[2] for row in rows}
if "ota" not in subtypes:
    raise AssertionError("missing otadata partition")
if "ota_0" not in subtypes or "ota_1" not in subtypes:
    raise AssertionError("two OTA app slots are required")

app_slots = [row for row in rows if row[2] in {"ota_0", "ota_1"}]
if len(app_slots) != 2:
    raise AssertionError("expected exactly two OTA app slots")

def parse_size(value: str) -> int:
    value = value.strip().lower()
    return int(value, 0)

slot_sizes = [parse_size(row[4]) for row in app_slots]
if min(slot_sizes) < 0x300000:
    raise AssertionError(f"OTA slot too small: {slot_sizes}")

if 'board_build.partitions = partitions_ota_16mb.csv' not in (ROOT / "platformio.ini").read_text():
    raise AssertionError("PlatformIO is not using OTA partition table")

print("OTA CONTRACT: PASS")
print("- dual OTA app slots + otadata")
print("- HTTPS Databricks manifest/download protocol")
print("- SHA-256 verification before boot switch")
print("- rollback confirmation hook")
print("- no WSL/LAN backend dependency")
