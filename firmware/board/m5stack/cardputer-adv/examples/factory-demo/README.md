# Cardputer-Adv factory demo

This example runs M5Stack’s original Mooncake launcher and its 13 menu apps. On a cold power-on, press a key at the splash screen. Use the arrow keys to browse, Enter to open an app, and G0 to go back. GPS and LoRa Chat use the corresponding external hardware.

The build fetches the [CardputerADV source revision](https://github.com/m5stack/M5Cardputer-UserDemo/tree/9d7190317d49ff87fa98db345b049ed77c5a1780) and verifies the archive checksum. The EspHub patch shares the SDK-managed Wi-Fi station with the launcher, saves networks through the SDK and keeps device provisioning in NVS. The original icons, startup animation and application code are built into the OTA image.

With ESP-IDF v5.5.4 active, run this command from the firmware repository root.

```sh
python3 scripts/build-example.py m5stack/cardputer-adv --example factory-demo
```
