# EspHub firmware

Firmware sources, board profiles, and build recipes for [EspHub](https://github.com/okou-ai/esphub).

## Build

From the **Board firmware** workflow, run a manual build with a canonical board ID such as `m5stack/stopwatch`. Manual runs build the selected board's components and USB kit. Pull requests and pushes build changed inputs. Download build results from the workflow's artifacts.

The initial extraction uses the existing EspHub releases to reuse previously published inputs where a build does not replace them. Production downloads and the live release catalog continue to be served by EspHub during the migration to R2 and Neon.

## Development

Run `npm run catalog` to generate board metadata. Run `npm test` with Node.js 24, a C compiler, pkg-config, and the cJSON development package. See the [board matrix](docs/m5stack-boards.md) for ESP32 targets.

## Production publishing

Enable the publishing job once the EspHub API serves the `esphub-firmware` R2 bucket. Each successful board build dispatches **Publish board firmware** separately after its components have passed. To retry a failed publication, dispatch that workflow on `main` with the trusted build run ID and board ID. The retry reuses the validated artifacts. The step verifies manifest hashes, writes immutable versioned objects and release receipts, then advances that board's Neon catalog in one transaction. Versions in this repository start at `1.2`.

Configure the `production` environment with `FIRMWARE_R2_ENDPOINT`, bucket-scoped `FIRMWARE_R2_ACCESS_KEY_ID` and `FIRMWARE_R2_SECRET_ACCESS_KEY`, and `FIRMWARE_DATABASE_URL` for a login limited to `USAGE` on `esphub` and `SELECT, INSERT, UPDATE` on `esphub.firmware_releases`. Set `FIRMWARE_PUBLISH_ENABLED=true` after configuring the environment. The R2 endpoint and enable flag are Actions variables.
