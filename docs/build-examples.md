# Build a board example

Check out the EspHub firmware revision in the example's source URL. Each board directory contains its default example, additional examples, entrypoints, descriptions and upstream references in `board.json`. The USB kit includes the default example. Each example builds into an ordinary ESP-IDF application image and starts the EspHub SDK.

For M5Stack apps, activate ESP-IDF v5.5.4. For ESP-Mosaico use the Espressif release-v6.2 container pinned in the firmware plan. Python and Git are required. From the repository root:

```sh
python3 scripts/build-example.py m5stack/stopwatch
python3 scripts/build-example.py m5stack/stopwatch --example buttons --out dist/buttons
```

Open the example's entrypoint to change its behavior. The build downloads pinned dependencies, writes the app binary and SHA-256 to the output directory, and checks the image against the board's application slot size. On a registered device enter Bootloader mode, upload the binary with `ota.prepare_upload` and install with `ota.install` using kind `app`. Verify the install receipt, the next app session and the behavior in the example description.

M5Stack examples are adapted from the manufacturer’s board guides and M5GFX/M5Unified library examples. See each example's upstream URL and the [M5Stack attribution](../firmware/board/m5stack/LICENSE). TriviaPOD ships proprietary game firmware. Its starter is Espressif's ESP32-S3 chip-info example and uses the serial console.
