# EspHub firmware

Firmware sources, board profiles, and build recipes for [EspHub](https://github.com/okou-ai/esphub).

## Build

From the **Board firmware** workflow, run a manual build with a canonical board ID such as `m5stack/stopwatch`. Manual runs build the selected board's components and USB kit. Pull requests and pushes build changed inputs. Download build results from the workflow's artifacts.

The initial extraction uses the existing EspHub releases to reuse previously published inputs where a build does not replace them. Production downloads and the live release catalog continue to be served by EspHub during the migration to R2 and Neon.

## Development

Run `npm run catalog` to generate board metadata. Run `npm test` with Node.js 24, a C compiler, pkg-config, and the cJSON development package. See the [board matrix](docs/m5stack-boards.md) for ESP32 targets.
