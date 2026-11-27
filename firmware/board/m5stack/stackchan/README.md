# M5Stack StackChan

The K151 robot combines the CoreS3-based controller with a separate body controller. Register it as `m5stack/stackchan` and flash the setup kit returned by `devices_create`. Follow `INSTALL.txt` to provision USB and Wi-Fi.

The factory application is [rgb-led](examples/rgb-led/main.cpp). Cycle eight colors across the twelve RGB LEDs on the body, six per side. Edit the palette or animation speed. The [board manifest](board.json) lists its upstream source and available examples. [Build your own application](../../../../docs/build-examples.md)
