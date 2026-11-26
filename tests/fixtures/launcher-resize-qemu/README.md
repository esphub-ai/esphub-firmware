# Launcher resize probe

This ESP32-S3 probe runs the production resize operation, ESP-IDF flash driver, NVS and the same LittleFS component as the board examples. It installs a real app image and cold-boots the same disposable flash three times. It checks that 4 → 5 → 4 MiB clears previous files on each change, each new volume mounts without formatting and accepts writes, and same-size retries preserve files. A missing erase authorization, incorrect filesystem seed and a shrink below the installed image size are rejected. The host compares the application and both Launcher slots byte for byte across all three boots.

With ESP-IDF v5.4.1 activated and Espressif's QEMU on `PATH`, run from the firmware repository root. The second build uses the previously shipped 16 MiB table with 2 MiB Launcher slots.

```sh
probe=tests/fixtures/launcher-resize-qemu
work=$(mktemp -d)
idf.py -C "$probe" -B "$work/new" -D IDF_TARGET=esp32s3 -D SDKCONFIG="$work/new.config" build
python "$probe/verify.py" "$work/new"
idf.py -C "$probe" -B "$work/legacy" -D IDF_TARGET=esp32s3 -D SDKCONFIG="$work/legacy.config" -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.legacy' build
python "$probe/verify.py" "$work/legacy"
```
