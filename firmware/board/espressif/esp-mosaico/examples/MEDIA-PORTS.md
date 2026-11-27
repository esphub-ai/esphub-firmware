# ESP-Mosaico BSP audio and model examples

All four examples use BSP commit [dbb7509](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/dbb7509e1be19c35fd091e6a81ac52826a8c58d6), Apache-2.0 by Espressif, and use EspHub SDK 8.0.1 for Wi-Fi credentials and connection lifecycle. The application code does not manage Wi-Fi. The builds use the standard EspHub 16 MiB NOR layout with an 8 MiB OTA application slot. Record built sizes before acceptance.

| Example | Resources and modules | Remaining runtime validation |
| --- | --- | --- |
| gmf-audio-player | Espressif Apache-2.0 alarm tone from esp-gmf, ES8311 speaker codec | MP3 decode, repeat and audible output |
| voice-tts | [ESP-SR 2.4.7](https://components.espressif.com/components/espressif/esp-sr/versions/2.4.7), Xiaole voice, ES8311 | Embedded voice loading and spoken output |
| voice-wake-cmd | ESP-SR 2.4.7, Xiaole voice and configured models, microphone and speaker | Embedded model loading, wake word, command parsing and spoken response |
| ai-model-gallery | OV3640 or SC101IOT camera, LCD and AI button; ESP-DL face and COCO model components | Camera preview, both inference modes and return to idle |

The speech builds embed the same upstream payloads in the app image so application-only OTA can use the unchanged partition table. ESP-SR's license permits use on Espressif products and requires retention of the copyright and permission notices. Collect resolved component license and model notices with firmware before distribution. No audio or model payloads are checked into this repository.

Board-level simulation is pending for all four applications.
