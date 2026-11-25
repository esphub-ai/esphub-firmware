# Frozen imports used by the official hardware.stackchan module and its peripherals.
freeze("$(MPY_DIR)/../m5stack/libs", [
    "hardware/__init__.py",
    "hardware/stackchan.py",
    "driver/__init__.py",
    "driver/m5ioe1.py",
    "driver/si12t.py",
    "driver/scs_servo.py",
    "driver/ina226.py",
    "driver/nfc.py",
    "driver/st25r3916.py",
    "driver/mifare_classic1.py",
])
