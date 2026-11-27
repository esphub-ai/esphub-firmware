# M5Stack StopWatch examples

## EspHub apps

Build a single application with ESP-IDF 5.5.4 from the repository root. The output is an EspHub OTA **app** image for the 4 MiB application partition.

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

The two submodules pin the original multi-app factory interface and power measurement projects. They can be checked out separately from the EspHub examples.

```sh
git submodule update --init --recursive firmware/board/m5stack/stopwatch/factory-demo firmware/board/m5stack/stopwatch/power-demo
```

- [Factory firmware](./factory-demo) contains the original launcher, apps, artwork and settings. Activate ESP-IDF 5.5.4, run `python3 fetch_repos.py` inside that project and build with `idf.py build`.
- [PowerDemo](./power-demo) contains 16 independent bench projects, including L2 deep sleep, IMU motion wake, RTC wake and Wi-Fi power measurements. Activate ESP-IDF 5.5.5 and build from an individual test directory, for example `power-demo/tests/imu_wake_l2`, with `idf.py build`. See the upstream test README for its wake sequence, measurement conditions and fixture requirements.

The two original projects use their own build configurations and USB installation procedures. For remote updates on an EspHub device, choose an application from the table above.
