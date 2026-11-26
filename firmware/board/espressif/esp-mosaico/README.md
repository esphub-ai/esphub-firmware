# Espressif ESP-Mosaico

ESP-Mosaico uses the `esp32s31-16mb` EspHub bootloader and boots from 16 MiB SPI NOR.
Its 16 MiB PSRAM and 128 MiB SPI NAND are separate from the boot flash.
Use the onboard USB-C port in ROM download mode to flash the kit. The bootloader uses UART0 on the right expansion header (H1) for registration and console output.
[Official board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/user_guide.html)

## Hello World example

The example uses Espressif's [Hello World template](https://github.com/esp-mosaico/esp-mosaico-utils/tree/f58f4f9e5363f908b17a8e1ea8fdf4dd77561ef1/mosaico-tools/templates/hello_world). It compiles the upstream GSP scene, assets, touch counter and display driver. The GSP bundle is embedded in the application image so the same binary works for application OTA and the first-time kit. EspHub's SDK uses the registration and Wi-Fi configuration saved by the bootloader and connects to EspHub over MQTT.

On a registered board, call `bootloader.enter` and wait for the bootloader to come online. Use `ota.install` with kind `app` and `https://api.esphub.ai/releases/espressif/esp-mosaico/app`. Check the install receipt and confirm that `devices.get` reports an online app with the expected version.

### Build locally

Activate the ESP-IDF release-v6.2 container pinned in the [firmware plan](../../../../scripts/firmware-plan.mjs). Run this from the firmware repository root. The builder checks out the sources in [upstream.json](examples/hello-world/upstream.json).

```sh
python3 scripts/build-example.py espressif/esp-mosaico --version 2.0.0
```

## First-time registration

Register the device as `espressif/esp-mosaico` in EspHub. Follow `INSTALL.txt` in the setup ZIP linked by the [setup manifest](https://api.esphub.ai/releases/espressif/esp-mosaico/setup-manifest). The kit contains the EspHub Bootloader and native Hello World app. The first-time flash replaces the partition table and application.

For boards running the former USB OTG bootloader, back up device data and flash the new setup kit to change bootloader profiles. Bootloader OTA validates the installed profile.

Connect a 3.3 V USB-to-UART adapter to H1. Connect adapter TX to pin 13 (RX0, GPIO59), adapter RX to pin 15 (TX0, GPIO58), and connect ground. Use the adapter port at 115200 baud with `provision_usb.py`, then configure Wi-Fi using the [Mac BLE tool](../../../../tools/ble-wifi/) while the board is in bootloader mode. Check that `devices.get` shows the device online, then call `bootloader.exit`. Verify Hello World and its touch counter on the display and the application version in `devices.get`.
