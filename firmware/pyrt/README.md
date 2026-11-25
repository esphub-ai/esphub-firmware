# MicroPython runtime integration

The MicroPython release workflow builds a runtime for each board using the source revision pinned in [upstream.json](upstream.json), the local overlay and ESP-IDF 5.5.5. Profiles select either the M5Stack UIFlow2 board port or the upstream MicroPython ESP32 port from its pinned submodule. Each port runs the ESPHub SDK. Generic profiles expose the standard MicroPython hardware APIs. The first-time setup ZIP includes the matching runtime, bootloader, partition table and esptool arguments. All first-time kits use a 4 MiB application slot and a LittleFS data partition. Installing this layout on a previously used board replaces its partition table and can destroy existing data.

The bootloader accepts the device token and BLE bootstrap key over USB and provisions Wi-Fi over BLE. The setup kit holds the first boot for provisioning. Start the installed application with `bootloader.exit`, the USB `app` command or a power cycle. On subsequent normal boots the ESP bootloader starts that partition directly. USB setup, recovery and firmware maintenance can return to the bootloader. The application SDK manages the hub connection and application input and log forwarding. The frozen boot module mounts the data partition at /flash and runs main.py. Scripts can use okou.wifi_rssi() to read the connected network's RSSI in dBm, or None if disconnected.

The runtime writes print() output to its local console. Python deployments use bootloader HTTPS OTA.

In the device workspace, **Firmware** shows the bootloader, interpreter and runtime build separately. The last confirmed cloud installation of `main.py` is available offline through Firmware or `programs.get`, with its transfer ID, SHA-256 and byte count. The runtime does not read or upload local program source during boot.

The bootloader leaves the board power controller in its existing state. On StopWatch, an application that arms the M5PM1 watchdog must disarm it before handing control to the bootloader for an update.

The [board matrix](../../docs/m5stack-boards.md) links product-specific firmware and examples. The [simulator checks](../../tools/micropython-sim/README.md) run the packaged program through the ESP runtime or the MicroPython Unix interpreter.
