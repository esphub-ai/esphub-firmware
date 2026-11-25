# M5Stack Cardputer-Adv

Register this ESP32-S3 board as `m5stack/cardputer-adv`. It has 8 MiB of flash, a 240 × 135 ST7789V2 display and a TCA8418 keyboard controller. The official UIFlow target is `M5STACK_CardputerADV`. [M5Stack hardware reference](https://docs.m5stack.com/en/core/Cardputer-Adv)

## USB setup

1. Download the first-time kit for this board and verify the ZIP against the SHA-256 in its manifest. The install command in the ZIP erases the entire flash. [USB kit](https://api.esphub.ai/releases/m5stack/cardputer-adv/setup) · [Manifest](https://api.esphub.ai/releases/m5stack/cardputer-adv/setup-manifest)
2. Use the port belonging to the Cardputer-Adv and follow `INSTALL.txt`. The bootloader runs the USB provisioning console at 115200 baud. Write the token and BLE bootstrap key returned by registration. Run the Mac BLE tool while the board is in the bootloader and select the Wi-Fi network on the Mac.
3. Wait for `OKL-PROVISION-OK` on USB. After Wi-Fi setup, call `bootloader.exit` or power-cycle the board. Check `devices.get` for an online device reporting role `app` and image profile `ESPHUB2/micropython/m5stack/cardputer-adv`. The USB device can re-enumerate when the bootloader starts MicroPython.

## Display and keyboard check

Deploy [main.py](main.py) using the Python OTA flow in the [repository README](../../../../README.md#ota). In the new application session, check for `ESP Hub hello drawn: m5stack/cardputer-adv 240 135`. Press the **A** key without Shift. The display should show `Key: a` and the application log should show `Keyboard event: 97`.

The example uses M5Stack's `MatrixKeyboard` API. The [frozen module manifest](micropython/manifest.py) includes the keyboard and TCA8418 drivers. A change to that manifest selects the Cardputer-Adv MicroPython and USB setup tasks in [ci.json](ci.json).
