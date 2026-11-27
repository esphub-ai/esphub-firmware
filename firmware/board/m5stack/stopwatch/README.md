# M5Stack StopWatch examples

## EspHub apps

Build a single application with ESP-IDF 5.5.4 from the repository root. The output is an EspHub OTA **app** image for the 8 MiB application partition of the 16 MiB layout.

```sh
python3 scripts/build-example.py m5stack/stopwatch --example stopwatch --out dist/stopwatch
```

| Example | Controls and behavior |
| --- | --- |
| `stopwatch` | Blue B starts, pauses and resumes. Yellow A marks a lap while running and resets while paused. The display keeps the latest four lap marks. |
| `fluidbox` | Tilt the device or tap the fluid to move the particles. A resets the simulation. Uses the upstream FluidBox solver and renderer. |
| `audio-fft` | The microphone drives 24 frequency bands and a peak-frequency readout. |
| `imu` | Tilt the board to move a ball inside a circular guide. |
| `watch-face` | Either button switches between analog and digital faces. The RX8130 keeps time offline and synchronizes to UTC when SNTP is available. |
| `alarm-clock` | B selects hours, minutes or the on/off field. A changes the selected value. The daily alarm is saved in NVS and sounds while this app is running. Tap or press either button to dismiss. |
| `lucky-wheel` | Choose 2–18 sectors with A/B, then tap to confirm. A and B spin in opposite directions. Tap to spin or hold the screen to change the number of sectors. |
| `badge` | Browse six saved PNG/JPEG slots with A/B. Send the app a message to replace an image. Stored images remain available offline. |
| `bar-graph` | Default USB setup app. Animates bars and peak markers. |
| `buttons` | Displays button transitions and logs click counts. |
| [`factory-demo`](./examples/factory-demo/) | Runs the complete M5Stack V0.5 menu as one OTA app. Hold A and B together to leave the first-boot guide, browse with either button and tap an icon to open an app. |

For Badge, pass a payload such as `{"slot":1,"url":"https://example.com/badge.png"}` to `devices_send`. Use a direct HTTPS image URL, up to 256 KiB. Square artwork sized for the 466 × 466 display works best. The app replies with `{"saved":true}` after decoding and saving the image. It stores files named `sw-badge-*.img` on the board's LittleFS data partition.

## Original M5Stack projects

The examples above are adapted from M5Stack's [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo), and `factory-demo` builds its V0.5 release. M5Stack's [M5StopWatch-PowerDemo](https://github.com/m5stack/M5StopWatch-PowerDemo) bench projects are installed over USB with their own build configurations and are not EspHub apps.
