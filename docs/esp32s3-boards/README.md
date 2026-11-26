# ESP32-S3 boards

| Manufacturer | Profiles | Hardware, factory apps and official examples |
| --- | ---: | --- |
| Waveshare | 94 | [Board index](waveshare.md) |
| Seeed Studio | 24 | [Board index](seeed-studio.md) |
| LILYGO | 11 | [Board index](lilygo.md) |

The profiles cover 8, 16 and 32 MiB flash configurations. The ESP32-S3 provides 2.4 GHz Wi-Fi and Bluetooth LE. Run the board’s factory example with ESP-IDF v5.5.4 using the [build instructions](../build-examples.md). Hardware guide and product links identify the intended memory SKU. Several kits share an MCU board and upstream examples.

## USB and flash configuration

Use the data port listed in the manufacturer's flashing guide. On boards with a USB-to-UART bridge the bootloader reads provisioning commands from UART0 at 115200 baud. On native USB boards it reads from USB Serial/JTAG. A board with an internal USB hub may enumerate both interfaces. Use the interface listed in the board index. For SenseCAP Indicator choose the CH340 port for the ESP32-S3. On Watcher the bottom USB-C port carries data and the second serial interface belongs to the AI processor.

The WROOM-2-N32R16V profiles use the octal-flash bootloader and configure their applications for octal flash. PSRAM wiring is selected separately. Bootloader images use internal SRAM only.

Some Waveshare products have several PCB revisions under one name. Their upstream examples carry revision labels in the index. The factory example uses the common peripheral where panel wiring varies. The WLED and HUB75 controllers require an external powered strip or matrix. The LCD Driver Board example expects a 240 × 240 ST7789 SPI panel.

## PCB revisions

| Waveshare product | Factory example and additional source examples |
| --- | --- |
| Touch-AMOLED-1.43 | The display app reads the panel ID and selects the onboard SH8601 or CO5300. |
| Touch-AMOLED-1.64 | The factory app checks the IMU. The V1 and V1.1 display examples select their respective chip-select pin. |
| Touch-AMOLED-1.8 | The factory app reads the RTC. Choose the V1 display example for SH8601Z and V2 for CO5300. |
| Touch-AMOLED-2.41 | The factory app reads the RTC. The RM690B0 display examples use GPIO reset on V1 and expander reset on V2. |
| Touch-LCD-3.49 | The factory app reads the RTC. The V1 and V2 backlight examples drive GPIO8 and GPIO42 respectively. |
| A7670E-4G and SIM7670G-4G | Both PCB revisions use GPIO38 for the LED. Use the USB-to-UART interface for EspHub provisioning. |

## Source and image credits

EspHub-written examples use the repository’s MIT license. The two Waveshare e-paper panel drivers retain their manufacturer notices in the corresponding source files. External components are fetched from their pinned releases or Git revisions and retain their own licenses. Upstream examples are linked with their original licensing terms.

Board photos are credited to the respective manufacturers. Each board index links the original image. Thumbnails are resized WebP renditions. Seeed Wiki images are available under [CC BY-SA 4.0](https://wiki.seeedstudio.com/License/). Other vendor materials retain their original terms. The T-Embed-Plus V2 profile links the manufacturer’s hardware guide while a verified photograph of this revision is unavailable.
