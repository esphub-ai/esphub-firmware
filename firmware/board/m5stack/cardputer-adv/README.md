# M5Stack Cardputer-Adv

Cardputer-Adv has 8 MiB flash, a 240 × 135 ST7789V2 display and a TCA8418 keyboard controller. Register it as `m5stack/cardputer-adv` and flash the setup kit returned by `devices_create`. Follow `INSTALL.txt` to provision USB and Wi-Fi.

The factory application is [display-shapes](examples/display-shapes/main.cpp). Draw randomly positioned colored circles and rectangles on the LCD. Change the primitives and colors in the drawing loop. The [board manifest](board.json) lists its upstream source and available examples. [Build your own application](../../../../docs/build-examples.md)

## Cardputer-Adv examples

Build an example by its catalog ID, for example:

```sh
python3 scripts/build-example.py m5stack/cardputer-adv --example keyboard-text
```

| Hardware | Example IDs | What to try |
| --- | --- | --- |
| BMI270 | `imu` | Tilt the board and compare the three acceleration and gyroscope axes. |
| Built-in keyboard | `keyboard-single`, `keyboard-multi`, `keyboard-text` | Press A, hold a chord, or edit and submit a line on the LCD. |
| HID keyboard | `keyboard-usb`, `keyboard-ble` | Connect a USB data cable or pair with **Cardputer ADV**, then type into a text editor on the host. The USB example uses the USB port for HID. |
| Microphone and speaker | `microphone`, `speaker-tones` | Press BtnA to record and replay a sample, or listen to the alternating tones. |
| microSD | `sd-card`, `record-wav` | Insert a FAT-formatted card before startup. Read back a greeting or press BtnA to save a new three-second WAV recording. |
| IR transmitter | `ir-nec` | Point at a NEC receiver and press BtnA. Compare the received address and command with the LCD. |
| Wi-Fi and clock | `wifi-scan`, `clock` | View nearby SSIDs and signal strength, or show UTC after Wi-Fi time synchronization. |

These examples adapt the [M5Cardputer Arduino sketches](https://github.com/m5stack/M5Cardputer/tree/bf64d3178ab43928fa02365fbe3937cec99716ad/examples), the [ADV IMU tutorial](https://docs.m5stack.com/en/arduino/m5cardputer/imu) and applications in [M5Stack's ADV factory demo](https://github.com/m5stack/M5Cardputer-UserDemo/tree/9d7190317d49ff87fa98db345b049ed77c5a1780/main/apps). Each catalog entry links its upstream source.
