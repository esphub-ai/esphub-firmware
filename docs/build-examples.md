# Build a board example

Check out the EspHub firmware revision in the example's source URL. Each board directory contains its default example, additional examples, entrypoints, descriptions and upstream references in `board.json`. The USB kit includes the default example. Each example builds into an ordinary ESP-IDF application image and starts the EspHub SDK.

For M5Stack, LILYGO, Waveshare and Seeed Studio apps, activate ESP-IDF v5.5.4. For ESP-Mosaico use the Espressif release-v6.2 container pinned in the firmware plan. Python and Git are required. From the repository root:

```sh
python3 scripts/build-example.py seeed-studio/xiao-esp32-s3
python3 scripts/build-example.py m5stack/stopwatch
python3 scripts/build-example.py m5stack/stopwatch --example buttons --out dist/buttons
```

Open the example's entrypoint to change its behavior. For native ESP-IDF examples, place additional C/C++ sources and headers next to that file. The builder compiles that directory as the main component. Reusable EspHub example headers live in `firmware/app/main/include/`. Place an ESP-IDF `idf_component.yml` beside the entrypoint for dependencies. The same directory can contain a `sdkconfig.defaults` for panel, console, or other example-specific settings. M5Unified boards can start from `firmware/app/m5.yml`.

For a board-local file shared by several examples, `board.json` can map its path relative to `examples/` to all consuming example IDs in `shared_example_inputs`. PR CI builds those examples when the file changes. A shared file used by the default app or without a declared list triggers a full board build.

The build downloads pinned dependencies, writes the app binary and SHA-256 to the output directory, and checks the image against the board's application slot size. Use the board’s setup kit and its `flash_args` for first-time USB installation. ESP-IDF’s generic `app-flash` command points at the first maintenance slot in this layout. On a registered device, use `bootloader.enter`, wait for the bootloader role, upload the app with `ota.prepare_upload` and call `ota.install` using kind `app`. Verify the install receipt, the next app session and the behavior in the example description.

M5Stack examples are adapted from the manufacturer’s board guides and M5GFX/M5Unified library examples. See each example's upstream URL and the [M5Stack attribution](../firmware/board/m5stack/LICENSE). TriviaPOD ships proprietary game firmware. Its starter is Espressif's ESP32-S3 chip-info example and uses the serial console.
