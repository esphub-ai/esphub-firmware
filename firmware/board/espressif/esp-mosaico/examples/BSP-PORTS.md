# ESP-Mosaico BSP ports

The builder fetches [Espressif's BSP at dbb7509](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/dbb7509e1be19c35fd091e6a81ac52826a8c58d6), applies the selected patch and builds the original project with EspHub SDK 8.0.1. The upstream Apache-2.0 license and source attribution remain in that checkout. The patches contain no separate binary assets.

All four use the EspHub 16 MiB NOR partition table and its 8 MiB OTA application slot. Sizes are measured by the build. The sample projects retain their upstream camera, display, and USB dependencies. The photo app uses separate SPI NAND for photos with USB mass storage disabled by default. Its NVS initialization reports errors without erasing EspHub settings.

| Example | Required modules | Network and remaining acceptance |
| --- | --- | --- |
| camera-lcd-preview | OV3640 camera, CO5300 LCD and PPA | SDK owns Wi-Fi. Verify startup and live preview. |
| camera-photo-app | OV3640 camera, LCD/touch, PPA, SPI NAND | SDK owns Wi-Fi. Verify preview, capture, gallery and NAND persistence. |
| usb-extend-screen | CO5300 LCD, USB HS, touch/audio modules as configured | SDK owns Wi-Fi. Verify USB enumeration, frame delivery, touch and audio. |
| wifi-http-cam | OV3640 camera and hardware JPEG encoder | SDK starts, then yields the radio to the camera's upstream SoftAP through `eh_wifi_set_managed(false)`. AP configuration stays in RAM. SDK STA is unavailable during camera hotspot use and restored if hotspot setup fails. Verify DHCP, JPEG and MJPEG and track physical RF separately. |

Run the build within the pinned ESP-IDF 6.2 environment.

```sh
python3 scripts/build-example.py espressif/esp-mosaico --example camera-lcd-preview
```

Build validation and peripheral simulation are separate. ESP32-S31 board simulation and the interactions above remain pending.
