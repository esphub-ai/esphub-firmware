import time

import M5
from M5 import Widgets

M5.begin()
Widgets.fillScreen(0x000000)
font = Widgets.FONTS.Montserrat14
title = Widgets.Label("CoreS3", 6, 6, 1.0, 0xffffff, 0x000000, font)
prompt = Widgets.Label("Touch the screen", 6, 30, 1.0, 0xffffff, 0x000000, font)
position = Widgets.Label("X: -  Y: -", 6, 56, 1.0, 0xffffff, 0x000000, font)
last = None
while True:
    M5.update()
    if M5.Touch.getCount():
        point = (M5.Touch.getX(), M5.Touch.getY())
        if point != last:
            position.setText("X: %d  Y: %d" % point)
        last = point
    else:
        last = None
    time.sleep_ms(20)
