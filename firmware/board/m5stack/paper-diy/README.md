# M5Stack PaperDIY

The K159 Assembly Kit and K159-B Complete Kit use the same 960 × 540 grayscale display and 16 MiB flash. Register it as `m5stack/paper-diy` and flash the setup kit returned by `devices_create`. Follow `INSTALL.txt` to provision USB and Wi-Fi.

The factory application is [scroll-text](examples/scroll-text/main.cpp). Scroll multilingual text on the display with an offscreen canvas. Replace the string or adjust the font and scroll speed. The [board manifest](board.json) lists its upstream source and available examples. [Build your own application](../../../../docs/build-examples.md)
