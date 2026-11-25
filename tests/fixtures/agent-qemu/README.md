# Agent FreeRTOS QEMU probe

Boot a real ESP-IDF v5.4.1 firmware image linking the production `okou_agent` component. This is not the host C shim. Both official QEMU `esp32` and `esp32s3` machines have passed.

## Coverage and limits

The probe observes the real Agent task's semaphore through `--wrap=xQueueSemaphoreTake`, forwarding every call to the actual FreeRTOS implementation. It verifies:

- Initially offline: infinite wait, no connection before an IP notification.
- IP notification wakes the Agent and starts transport, with a finite 30-second connection deadline.
- Online idle: `portMAX_DELAY`, with no semaphore returns or repeated calls during the observation window.
- Queued SDK event wakes the task and publishes on that task.
- Modeled MQTT control notification wakes the task and dispatches a registered command through the real SDK registry.
- Modeled disconnect wakes the task, enters finite jittered backoff and reconnects on a real FreeRTOS deadline **without another notification**.

Only network startup/IP availability and the `okou_iot_*` transport boundary are linker-wrapped. NVS, Agent state/queues, SDK command dispatch and task scheduling are real firmware. The seeded `qemu.invalid` hub and dummy token are not credentials. No broker, TLS/QoS, physical hardware or real Wi-Fi radio is exercised. OpenETH configuration avoids Wi-Fi initialization; no Ethernet device is needed because network startup is wrapped. Wi-Fi scan behavior is covered separately by host radio-model tests. OTA is outside this probe.

## One-time tool setup (Linux x86-64)

Keep downloads/tools outside the repository. The example uses `$HOME/.cache/esphub-sim`; choose a volume with at least 4 GiB free. Required existing packages: Git, curl, xz-utils, CMake, a host C compiler and Python 3 with venv support.

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends libslirp0 ninja-build
export SIM_ROOT="$HOME/.cache/esphub-sim"
mkdir -p "$SIM_ROOT"
git clone --depth 1 --branch v5.4.1 --single-branch \
  https://github.com/espressif/esp-idf.git "$SIM_ROOT/esp-idf"
export IDF_PATH="$SIM_ROOT/esp-idf"
export IDF_TOOLS_PATH="$SIM_ROOT/idf-tools"
python3 "$IDF_PATH/tools/idf_tools.py" install xtensa-esp-elf esp-rom-elfs
python3 "$IDF_PATH/tools/idf_tools.py" install-python-env --features=core
# IDF initializes the submodules needed by the selected components during CMake setup.

QEMU_TAG=esp-develop-9.2.2-20261109
QEMU_ARCHIVE=qemu-xtensa-softmmu-esp_develop_9.2.2_20261109-x86_64-linux-gnu.tar.xz
curl -fL "https://github.com/espressif/qemu/releases/download/$QEMU_TAG/$QEMU_ARCHIVE" \
  -o "$SIM_ROOT/$QEMU_ARCHIVE"
curl -fL "https://github.com/espressif/qemu/releases/download/$QEMU_TAG/qemu-esp_develop_9.2.2_20261109-checksum.sha256" \
  -o "$SIM_ROOT/qemu-checksum.sha256"
(cd "$SIM_ROOT" && grep "$QEMU_ARCHIVE$" qemu-checksum.sha256 | sha256sum -c -)
tar -xJf "$SIM_ROOT/$QEMU_ARCHIVE" -C "$SIM_ROOT"
```

## Environment and run

Use targeted exports, not `export.sh`: a minimal tool installation intentionally omits unrelated RISC-V/OpenOCD/ULP tools that the full export script may require. `ESP_ROM_ELF_DIR` is necessary for IDF v5.4.1's generated gdbinit even when no debugger is used.

```sh
export SIM_ROOT="$HOME/.cache/esphub-sim"
export IDF_PATH="$SIM_ROOT/esp-idf"
export IDF_TOOLS_PATH="$SIM_ROOT/idf-tools"
PY_VERSION=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf5.4_py${PY_VERSION}_env"
export ESP_ROM_ELF_DIR="$IDF_TOOLS_PATH/tools/esp-rom-elfs/20241011"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$IDF_TOOLS_PATH/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin:$PATH"

# From the EspHub repository root; separate build directory per chip.
python tests/fixtures/agent-qemu/run.py \
  --qemu "$SIM_ROOT/qemu/bin/qemu-system-xtensa" \
  --target esp32s3 --build-dir "$SIM_ROOT/probe-esp32s3"
python tests/fixtures/agent-qemu/run.py \
  --qemu "$SIM_ROOT/qemu/bin/qemu-system-xtensa" \
  --target esp32 --build-dir "$SIM_ROOT/probe-esp32"
```

The runner disables the component manager (no lock file or registry dependency is needed on IDF 5.4.1), puts sdkconfig/build/image/logs outside the repository, and fails on missing final PASS, panic, assertion or timeout. It watches for another 200 ms after the final marker, then terminates QEMU. `--seconds` defaults to 20.

Equivalent build/image/run commands for ESP32-S3:

```sh
export IDF_COMPONENT_MANAGER=0
PROBE="$PWD/tests/fixtures/agent-qemu"
BUILD="$SIM_ROOT/probe-esp32s3"
python "$IDF_PATH/tools/idf.py" -C "$PROBE" -B "$BUILD" \
  -D "SDKCONFIG=$BUILD/sdkconfig" -D IDF_TARGET=esp32s3 build
(cd "$BUILD" && python -m esptool --chip esp32s3 merge_bin \
  -o flash.bin --fill-flash-size 4MB @flash_args)
"$SIM_ROOT/qemu/bin/qemu-system-xtensa" -M esp32s3 \
  -display none -monitor none -serial "file:$BUILD/uart.log" \
  -no-reboot -snapshot -nic none \
  -drive "file=$BUILD/flash.bin,if=mtd,format=raw"
```

The last command runs indefinitely; prefer the runner for automated pass/fail and cleanup. File-backed UART is deliberate: stdio redirected through some sandbox shells can appear blank even while firmware runs. Expected final marker:

```text
ESPHUB-AGENT-QEMU PASS ALL starts=2 stops=1 commands=1 published=2
```

Generated evidence is in the chosen build directory: `build.log`, `merge.log`, `uart.log`, `qemu.log`, `qemu-command.json`. Do not commit these files, `dependencies.lock`, generated sdkconfig or firmware binaries.
