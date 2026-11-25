import time

import M5
from M5 import Widgets

M5.begin()
Widgets.fillScreen(0xffffff)
font = Widgets.FONTS.Montserrat24
title = Widgets.Label("PaperS3", 6, 6, 1.0, 0x000000, 0xffffff, font)
prompt = Widgets.Label("Tap to turn page", 6, 56, 1.0, 0x000000, 0xffffff, font)
page_label = Widgets.Label("Page 1", 6, 112, 1.0, 0x000000, 0xffffff, font)
page = 1
was_touched = False
while True:
    M5.update()
    touched = M5.Touch.getCount() > 0
    if touched and not was_touched:
        page += 1
        page_label.setText("Page %d" % page)
    was_touched = touched
    time.sleep_ms(50)
