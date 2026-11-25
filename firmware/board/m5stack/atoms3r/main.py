import time

import M5
from M5 import Widgets

M5.begin()
Widgets.fillScreen(0x000000)
font = Widgets.FONTS.Montserrat14
title = Widgets.Label("AtomS3R", 6, 6, 1.0, 0xffffff, 0x000000, font)
prompt = Widgets.Label("Press the screen", 6, 30, 1.0, 0xffffff, 0x000000, font)
counter = Widgets.Label("Presses: 0", 6, 56, 1.0, 0xffffff, 0x000000, font)
presses = 0
while True:
    M5.update()
    if M5.BtnA.wasClicked():
        presses += 1
        counter.setText("Presses: %d" % presses)
    time.sleep_ms(20)
