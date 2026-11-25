# M5Stack Cardputer-Adv

Cardputer-Adv has 8 MiB flash, a 240 × 135 ST7789V2 display and a TCA8418 keyboard controller. Register it as `m5stack/cardputer-adv` and flash the setup kit returned by `devices.create`. Follow `INSTALL.txt` to provision USB and Wi-Fi.

The factory application is [display-shapes](examples/display-shapes/main.cpp). Draw randomly positioned colored circles and rectangles on the LCD. Change the primitives and colors in the drawing loop. The [board manifest](board.json) lists its upstream source and available examples. [Build your own application](../../../../docs/build-examples.md)
