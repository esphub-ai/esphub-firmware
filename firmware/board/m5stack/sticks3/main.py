import time

import M5
from M5 import Widgets

M5.begin()
Widgets.fillScreen(0x000000)
font = Widgets.FONTS.Montserrat14
title = Widgets.Label("StickS3", 6, 6, 1.0, 0xffffff, 0x000000, font)
prompt = Widgets.Label("Press A or B", 6, 30, 1.0, 0xffffff, 0x000000, font)
counter = Widgets.Label("A: 0  B: 0", 6, 56, 1.0, 0xffffff, 0x000000, font)
a = b = 0
while True:
    M5.update()
    pressed_a = M5.BtnA.wasPressed()
    pressed_b = M5.BtnB.wasPressed()
    if pressed_a or pressed_b:
        a += int(pressed_a)
        b += int(pressed_b)
        counter.setText("A: %d  B: %d" % (a, b))
    time.sleep_ms(20)
