# MicroPython simulator checks

Run the firmware and its original `main.py` from a board-specific setup ZIP. The runner checks the kit SHA-256 and creates a disposable flash image with the same binaries and partition table. It selects the runtime OTA slot and seeds LittleFS with the kit's unmodified program and a test `boot.py`. The production startup code mounts the filesystem and starts `main.py`.

## ESP system simulation

Build Espressif QEMU on Linux with its usual build dependencies, including Ninja, libglib, libpixman and libslirp. From the EspHub repository root:

```sh
git clone --depth=1 --branch esp-develop-9.2.2-20261109 https://github.com/espressif/qemu.git esp-qemu
git -C esp-qemu apply "$(pwd)/tools/micropython-sim/qemu.patch"
mkdir -p esp-qemu/build esp-qemu/build-rv
(cd esp-qemu/build && ../configure --target-list=xtensa-softmmu --disable-docs --disable-werror --enable-slirp && ninja qemu-system-xtensa)
(cd esp-qemu/build-rv && ../configure --target-list=riscv32-softmmu --disable-docs --disable-werror --enable-slirp && ninja qemu-system-riscv32)
python3 -m venv .sim-venv
.sim-venv/bin/pip install littlefs-python==0.19.0
.sim-venv/bin/python tools/micropython-sim/run.py path/to/board-setup.json --qemu-source esp-qemu
```

Keep the setup JSON and ZIP in the same directory. Substitute the downloaded JSON filename. The process requires the exact board-specific UART pass marker and checks for a panic for another two seconds. Use `--seconds 120` to allow a slow host more startup time.

The patch models status registers needed during RF/ADC initialization, flash transfer timing, ESP32-S3 32 MiB flash, context-save registers and the M5Paper display-ready input. The tests exercise partition access, Python execution and the application output. Radio links, antenna performance, provisioning and physical panels require hardware.

Seven interactive examples use deterministic panel, button, keyboard or touch inputs. Their UART output also contains an `ESPHUB-QA PANEL` marker. Pass `--native-panel` for the additional native graphics probe. On StickS3, AtomS3R, CoreS3 and PaperS3 that probe forwards graphics to the firmware's M5 bindings while retaining modeled inputs. A native probe may time out or report a missing peripheral.

## MicroPython Unix simulation

Build the Unix interpreter from the MicroPython revision used for these checks, then pass its executable and the kit's manifest to the host runner. This check models device APIs and validates example behavior, including the offline and connected RSSI branches of the console example.

```sh
git clone https://github.com/micropython/micropython.git mp
git -C mp checkout 78ff170de9e32c79db6e64d3e33d2bd60002bdcd
make -C mp/ports/unix submodules
make -C mp/mpy-cross
make -C mp/ports/unix
python3 tools/micropython-sim/run-host.py path/to/board-setup.json --micropython mp/ports/unix/build-standard/micropython
```
