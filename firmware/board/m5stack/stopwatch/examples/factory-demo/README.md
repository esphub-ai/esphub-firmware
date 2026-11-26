# StopWatch factory demo

This example builds the eight-app M5Stack V0.5 release. Hold A and B together to leave the first-boot guide, browse with either button and tap an icon to open its app. In Stopwatch, B starts or pauses timing. A records a lap while running and resets after a pause. Hold both buttons to return to the menu. [Factory guide](https://docs.m5stack.com/en/guide/stopwatch/usage) · [V0.5 source and release](https://github.com/m5stack/M5StopWatch-UserDemo/releases/tag/V0.5)

The entrypoint starts EspHub before the factory launcher. The badge editor temporarily uses the watch's own access point and restores station mode on exit. Badge images use the board's LittleFS data partition. On first use, the application can initialize an erased partition. Factory settings and alarm resets use app-specific NVS namespaces. Hardware power-key timing and reset remain under PMIC control.

Build from the firmware repository root with ESP-IDF 5.5.4.

```sh
python3 scripts/build-example.py m5stack/stopwatch --example factory-demo
```

The build fetches immutable sources. The factory app, icons and boot sound come from M5Stack's release tree. The font subsets are derived from Commissioner, Montserrat and Maple Mono. Attribution and license texts for the application and those fonts are in NOTICE.txt. Libraries retain their upstream notices.
