# EspHub firmware

Firmware sources, board profiles, and build recipes for [EspHub](https://github.com/okou-ai/esphub).

## Build

From the **Board firmware** workflow, run a manual build with a canonical board ID such as `m5stack/stopwatch`. Manual runs build the selected board's components and USB kit. Pull requests and pushes build changed firmware inputs. Use a targeted manual run to validate changes to the workflows or shared USB-kit packaging action. Download build results from the workflow's artifacts.

USB kits use components built in the same workflow run. Shared bootloader builds are cached by input fingerprint. Published releases are served from R2 and indexed in Neon.

## Development

Run `npm run catalog` to generate board metadata. Run `npm test` with Node.js 24, a C compiler, pkg-config, and the cJSON development package. See the [board matrix](docs/m5stack-boards.md) for ESP32 targets.

SDK version automation runs here on PRs. It bumps changes to the SDK on same-repository branches, using a patch bump by default and the `sdk:minor` or `sdk:major` label when supplied. Fork contributors update the SDK version themselves.

## Remote console

Use `logs.set` to redirect ordinary ESP-IDF task logs and MicroPython stdout over the existing device MQTT connection. Capture starts off at boot. Only an enabled session allocates the ring and its send buffers. Device owners can page through output with `logs.read`. See the [wire protocol and memory limits](https://github.com/okou-ai/esphub/blob/main/docs/iot.md#remote-console).

## Production publishing

Enable the publishing job once the EspHub API serves the `esphub-firmware` R2 bucket. Each successful board build dispatches **Publish board firmware** separately after its components have passed. To retry a failed publication, dispatch that workflow on `main` with the trusted build run ID and board ID. The retry reuses the validated artifacts. The step verifies manifest hashes, writes immutable versioned objects and release receipts, then advances that board's Neon catalog in one transaction. Versions in this repository start at `1.2`.

Configure the `production` environment with `FIRMWARE_R2_ENDPOINT`, bucket-scoped `FIRMWARE_R2_ACCESS_KEY_ID` and `FIRMWARE_R2_SECRET_ACCESS_KEY`, and `FIRMWARE_DATABASE_URL` for a login limited to `USAGE` on `esphub` and `SELECT, INSERT, UPDATE` on `esphub.firmware_releases`. Set `FIRMWARE_PUBLISH_ENABLED=true` after configuring the environment. The R2 endpoint and enable flag are Actions variables.

## Application messages

EspHub sends a JSON envelope with `id` and `payload`. Messages use MQTT QoS 0 and expire with the connection.

Native applications call `okou_message_receive()` to take a heap-owned JSON envelope, and `okou_message_send(json, reply_to)` to send JSON text with an optional incoming ID. Use `NULL` for an unsolicited message. A successful send means the local transport queue accepted the message.

MicroPython exposes the same interface:

```python
import json
import okou

message = okou.message_receive()
if message is not None:
    envelope = json.loads(message)
    okou.message_send(json.dumps({"received": envelope["payload"]}), envelope["id"])
```

Read device messages through the Hub's `logs.read` tool.
