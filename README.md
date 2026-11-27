# EspHub firmware

Firmware sources, board profiles, and build recipes for [EspHub](https://github.com/esphub-ai/esphub).

## Build

Each board declares a default application and its examples in `board.json`. Each board publishes only its default app. Boards with the same bootloader ID in `board.json`, such as `esp32s3-16mb`, share one bootloader set: the ESP-IDF second-stage bootloader, the partition table and the EspHub Launcher. The Hub combines a board's bootloader set, its app and a private NVS image into a first-install flash plan. The catalog exposes a description, upstream reference, pinned EspHub source, entrypoint and build command for each example. Follow the [build guide](docs/build-examples.md) to edit one.

Start the SDK from the application entrypoint:

```c
#include "eh_agent.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    // Start the application here.
}
```

From the **Board firmware** workflow, run a manual build with a canonical board ID such as `m5stack/stopwatch`. Manual runs build the selected board's app and its bootloader set. Pull requests and pushes build changed board apps and changed bootloader sets. Download build results from the workflow's artifacts.

Bootloader set builds are cached by input fingerprint. Published releases are served from R2 and indexed in Neon.

## Development

Run `npm run catalog` to validate board metadata. Run `npm test` with Node.js 24, a C compiler, pkg-config, and the cJSON development package.

SDK version automation runs here on PRs. It bumps changes to the SDK on same-repository branches, using a patch bump by default and the `sdk:minor` or `sdk:major` label when supplied. Fork contributors update the SDK version themselves.

## Remote console

Use `logs_set` to redirect ordinary ESP-IDF task logs over the existing device MQTT connection. Capture starts off at boot. Only an enabled session allocates the ring and its send buffers. Device owners can page through output with `logs_read`.

## Production publishing

Each successful board app build dispatches **Publish board firmware**, and each successful bootloader set build dispatches **Publish a bootloader set**. To retry a failed publication, dispatch the workflow on `main` with the trusted build run ID and the board or bootloader ID. A retry reuses the validated artifacts. Board publication verifies the app hash, writes the immutable app image and advances that board's Neon catalog in one transaction. Bootloader publication verifies the descriptor, writes `bootloaders/<id>/<version>/{bootloader,partition-table,launcher}.bin` and records one `esphub.bootloaders` row with each file's flash offset, R2 key and SHA-256, the flash settings, and the app and NVS offsets. The Launcher image also serves Launcher OTA. The same transaction records every published file in `esphub.release_assets` and the tag of the latest release, so the Hub serves downloads from Neon without this repository's release naming. Versions in this repository start at `2.0`.

Configure the `production` environment with `FIRMWARE_R2_ENDPOINT`, bucket-scoped `FIRMWARE_R2_ACCESS_KEY_ID` and `FIRMWARE_R2_SECRET_ACCESS_KEY`, and `FIRMWARE_DATABASE_URL` for a login limited to `USAGE` on `esphub` and `SELECT, INSERT, UPDATE` on `esphub.firmware_releases`, `esphub.board_examples`, and `esphub.board_sources`, plus `DELETE` on `esphub.board_sources` and `SELECT, INSERT` on `esphub.release_assets` and `esphub.bootloaders`. Set `FIRMWARE_PUBLISH_ENABLED=true` after configuring the environment. The R2 endpoint and enable flag are Actions variables.

Board hardware definitions and photos live in [esphub-boards](https://github.com/esphub-ai/esphub-boards). Example catalog changes on main are published to `esphub.board_examples` and `esphub.board_sources` in Neon independently of device binary releases. Each board keeps its `vendor/model` ID. The Hub joins board specs from `esphub.boards` with these example/source links and the latest published firmware versions at request time. Retired boards keep the links of their pinned revision.

Pull requests that add or edit optional examples compile just those examples on their respective boards. Changes to the default app, board configuration or shared firmware inputs run the full build for each affected board. Merging cancels that PR's remaining board builds.

On `main`, the planner checks all changes since `ci/firmware-coverage`, including changes that were later reverted. A board example selects its own board. Shared SDK or build-recipe changes can select multiple boards. Missing or cancelled work keeps the revision pending. Applications compile in shared shards first. A compile error fails its shard, which skips the board jobs and the coverage record, so the revision stays pending and the next run retries it. A board job that fails after its shard succeeded counts as an attempt and stays visible in CI.

A newer push replaces a batch that is still waiting, since the next batch covers its changes. An hourly check dispatches outstanding builds. To catch up immediately, dispatch **Board firmware** on `main` with the board field empty.

## Application messages

EspHub sends a JSON envelope with `id` and `payload`. Messages use MQTT QoS 0 and expire with the connection.

Native applications call `eh_message_receive()` to take a heap-owned JSON envelope, and `eh_message_send(json, reply_to)` to send JSON text with an optional incoming ID. Use `NULL` for an unsolicited message. A successful send means the local transport queue accepted the message.

Read device messages through the Hub's `logs_read` tool.
