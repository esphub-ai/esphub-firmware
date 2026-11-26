# M5Stack PaperColor factory photo frame

M5Stack's User Demo 1.0.1 has a local photo slideshow and an EzData cloud frame.
The build fetches the pinned upstream source, checks the archive SHA-256 and
applies the board-local changes. The upstream repository uses the MIT license.
See [the pinned source](https://github.com/m5stack/M5PaperColor-UserDemo/tree/884a7b7ebe1d5f1ca55c0dc4864f7708636677ea)
and [the upstream notice](LICENSE.upstream).

The entry point starts the EspHub SDK before the factory application. The SDK
owns saved networks, scans, station joins and reconnection. The factory web UI
requests portal mode from the SDK and migrates the previous saved station on
first use. Portal mode and station pause are opt-in SDK additions in this
experimental branch.

Build from the repository root with ESP-IDF 5.5.4 active.

```sh
python3 scripts/build-example.py m5stack/paper-color --example factory-demo \
  --version 1.0.1-sdk8-lab --out dist
```

This port is pending board-level validation. The factory keeps its wear-levelled
FAT and SD backends. On the standard EspHub flash layout, the app stores photos
in its own directory on the shared LittleFS volume. It initializes that volume
only if every byte is erased. USB mass storage for the internal LittleFS gallery
and physical SD switching still require integration and verification. Native
ESP32-S3 QEMU stops in the Wi-Fi PHY calibration during startup.
