# Raylib Lite Engine examples

The six applications come from [Espressif Component Registry 0.1.1](https://components.espressif.com/components/espressif2022/raylib-lite-engine/versions/0.1.1), the version pinned by ESP-Mosaico Vibe at `1fbf0f704e8a30d2acdc7d4416d5895cfc96dd45`. Per-example manifests pin the official archives by SHA-256. The build uses the original games and assets directly from their archives. The shared patch pins the engine to 0.1.1 and starts EspHub SDK 8.0.1 from the native launcher.

Each archive includes its Apache-2.0 license and its own `ASSET_PROVENANCE.json` with media attribution and hashes. The engine retains raylib and transitive notices documented in [THIRD_PARTY_NOTICES.md](https://github.com/espressif2022/raylib-lite-engine/blob/040b84e75640c325bca45515a4135f4b1640fd82/THIRD_PARTY_NOTICES.md). No game assets are tracked by this change. Before distributing firmware, collect notices for the exact resolved dependency set.

| Registry example | Board dependencies exercised by the game |
| --- | --- |
| raylib_shooter | LCD, touch |
| tower_defense | LCD, touch, audio |
| sky_hop | LCD, touch, audio |
| living_worlds | LCD, touch, audio |
| last_zone_extraction | LCD, touch, audio |
| neon_rift_rally | LCD, touch, audio, motion and haptic support where available |

The native projects use each archive's ESP-Mosaico Board Manager profile. They have no game-layer Wi-Fi. EspHub SDK owns credentials and the STA lifecycle. All use the EspHub 16 MiB NOR layout with an 8 MiB application slot and per-build size validation. Native image sizes and OTA boot remain to be verified. The upstream `tomb_raycast` is an internal preview and `render_benchmark` is a separate performance project, both absent from this Registry release's published games.

The source Host simulator ran 120 frames of each game at revision `040b84e75640c325bca45515a4135f4b1640fd82`. This covers native Host rendering and game startup only. ESP32-S31 boot, SDK networking, hardware inputs and the complete game paths require separate verification.
