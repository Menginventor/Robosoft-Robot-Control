"""
Export compiled PlatformIO firmware for M5Stick-C as standalone HEX and BIN files.
Combines:
  - 0x1000: bootloader.bin
  - 0x8000: partitions.bin
  - 0xe000: boot_app0.bin
  - 0x10000: firmware.bin
"""

import os
import subprocess
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(BASE_DIR, ".pio", "build", "m5stick-c")
EXPORT_DIR = os.path.join(BASE_DIR, "export")
os.makedirs(EXPORT_DIR, exist_ok=True)

PYTHON_EXE = os.path.expanduser(r"~/.platformio/penv/Scripts/python.exe")
ESPTOOL_PY = os.path.expanduser(r"~/.platformio/packages/tool-esptoolpy/esptool.py")
BOOT_APP0 = os.path.expanduser(r"~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")

bootloader = os.path.join(BUILD_DIR, "bootloader.bin")
partitions = os.path.join(BUILD_DIR, "partitions.bin")
app = os.path.join(BUILD_DIR, "firmware.bin")

for f in [bootloader, partitions, app, BOOT_APP0]:
    if not os.path.exists(f):
        print(f"[ERROR] Missing file: {f}")
        print("Please run `pio run` first!")
        sys.exit(1)

flash_args = [
    "0x1000", bootloader,
    "0x8000", partitions,
    "0xe000", BOOT_APP0,
    "0x10000", app
]

print("1. Generating export/firmware_merged.hex (Intel HEX format)...")
subprocess.check_call([
    PYTHON_EXE, ESPTOOL_PY, "--chip", "esp32", "merge_bin",
    "-o", os.path.join(EXPORT_DIR, "firmware_merged.hex"),
    "--format", "hex",
    "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB",
    *flash_args
])

print("2. Generating export/firmware_merged.bin (Raw merged binary at 0x0)...")
subprocess.check_call([
    PYTHON_EXE, ESPTOOL_PY, "--chip", "esp32", "merge_bin",
    "-o", os.path.join(EXPORT_DIR, "firmware_merged.bin"),
    "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB",
    *flash_args
])

print("\nDone! Export files created in:")
print(f"  {EXPORT_DIR}")
