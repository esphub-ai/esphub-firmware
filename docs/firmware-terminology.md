# EspHub firmware and flash layout

| Component | Role | Flash location |
| --- | --- | --- |
| ESP-IDF bootloader | Reads the partition table and OTA selection and boots an ESP image. | Second-stage bootloader region. |
| EspHub Bootloader | Provides USB/BLE provisioning, recovery and binary OTA. | `launcher_a` and `launcher_b` use ESP-IDF subtypes `ota_0` and `ota_1`. These labels and their maintenance role belong to EspHub. |
| Application | Runs a board example or custom firmware. | `app` uses subtype `ota_2`. |
| EspHub SDK | Connects a bootloader or application to the Hub. Applications call `eh_agent_start()`. | Linked into the image. |

## Updates and recovery

A new USB kit installs the ESP-IDF bootloader, partition table, EspHub Bootloader, initial configuration and default example. Applications and the maintenance firmware share network credentials in NVS. The active EspHub Bootloader can install either an application or a replacement bootloader.

A bootloader update validates the inactive maintenance slot before selecting it. Application OTA writes the single `app` slot. The maintenance firmware stays available to retry an interrupted download. An image that passes validation and then crashes has no automatic application rollback. A physical recovery path or a USB kit may be necessary.

## Capacity

Both Launcher slots are 1.25 MiB on every flash size. The default application and LittleFS sizes per flash size, and how `ota.resize_app` moves the boundary between them, are in [App and filesystem capacity](build-examples.md#app-and-filesystem-capacity). The partition tables are in [`firmware/partitions/`](../firmware/partitions/).

The `data` partition is available to the application. NVS stores credentials and pending receipts. `otadata` holds ESP-IDF boot selection and `phy_init` stores radio configuration. OTA uses the partition table already on the board.

## Build a bootloader locally

Each board's `firmware/board/<group>/<project>/board.json` names its `bootloader` variant in `firmware/bootloader/`. Use the ESP-IDF release that CI selects in `scripts/firmware-plan.mjs`: 6.1.1 for `esp32s31` variants, 5.5.4 for `esp32c5` and `esp32c61`, and 5.4.1 for all others. For example, build the 16 MiB ESP32-S3 variant:

```sh
cd firmware/bootloader/esp32s3-16mb
idf.py build
```

CI packages release builds with `scripts/build-bootloader.sh`.
