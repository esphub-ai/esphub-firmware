# Espressif ESP-Mosaico

ESP-Mosaico uses the `esp32s31-16mb-usb-cdc` EspHub bootloader and boots from 16 MiB SPI NOR.
Its 16 MiB PSRAM and 128 MiB SPI NAND are separate from the boot flash.
Use the onboard USB-C port for bootloader provisioning over USB OTG CDC and UART0 for early boot logs.
[Official board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/user_guide.html)

## Native example

The example runtime uses Espressif's [Hello World template](https://github.com/esp-mosaico/esp-mosaico-utils/tree/f58f4f9e5363f908b17a8e1ea8fdf4dd77561ef1/mosaico-tools/templates/hello_world). It compiles the upstream GSP scene, assets, touch counter and display driver. The GSP bundle is embedded in the application image so the same binary works for native OTA and the first-time kit. EspHub's SDK uses the registration and Wi-Fi configuration saved by the bootloader and connects to EspHub over MQTT.

On a registered board, call `bootloader.enter` and wait for the bootloader to come online. Use `ota.install` with kind `native` and `https://api.esphub.ai/releases/espressif/esp-mosaico/native`. Check the install receipt and confirm that `devices.get` reports an online native app with the expected version.

### Build locally

Use the ESP-IDF 6.2 release branch image pinned in [ci.json](ci.json) and checkouts of ESP-Mosaico BSP and ESP-Mosaico Utils at the revisions in [native/upstream.json](native/upstream.json). From this board directory, with the IDF environment active:

```sh
export MOSAICO_BSP=/absolute/path/to/esp-mosaico-bsp
export MOSAICO_UTILS=/absolute/path/to/esp-mosaico-utils
idf.py -C native/project -DPROJECT_VER=1.0.0 app
```

The OTA image is `native/project/build/mosaico_hello.bin`. Use the EspHub partition layout for installation.

## First-time registration

Register the device as `espressif/esp-mosaico` in EspHub. Follow `INSTALL.txt` in the setup ZIP linked by the [setup manifest](https://api.esphub.ai/releases/espressif/esp-mosaico/setup-manifest). The kit contains the EspHub Bootloader and native Hello World app. The first-time flash replaces the partition table and application.

Write the private registration over USB CDC, then configure Wi-Fi using the [Mac BLE tool](../../../../tools/ble-wifi/) while the board is in bootloader mode. Check that `devices.get` shows the device online, then call `bootloader.exit`. Verify Hello World and its touch counter on the display and the native runtime version in `devices.get`.
