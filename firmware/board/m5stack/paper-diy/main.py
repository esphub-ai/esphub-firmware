import time

import M5
import okou
from M5 import Widgets

M5.begin()
assert M5.getBoard() == M5.BOARD.M5PaperDIY, 'Expected PaperDIY'
Widgets.setRotation(0)
Widgets.fillScreen(0xffffff)
font = Widgets.FONTS.Montserrat24
def label(text, y):
    return Widgets.Label(text, 24, y, 1.0, 0x000000, 0xffffff, font)

title = label('Hello, M5Stack PaperDIY', 40)
credit = label('Powered by ESPHub.ai', 90)
rssi = okou.wifi_rssi()
wifi = label('Wi-Fi: %s' % ('offline' if rssi is None else '%d dBm' % rssi), 160)
battery = label('Battery: %d mV' % M5.Power.getBatteryVoltage(), 210)
counter = label('A: 0   B: 0', 320)
M5.Display.setEpdMode(M5.Display.EPDMode.EPD_FASTEST)
print('ESP Hub hello drawn: m5stack/paper-diy')
a = b = 0
while True:
    M5.update()
    pressed_a, pressed_b = M5.BtnA.wasPressed(), M5.BtnB.wasPressed()
    if pressed_a or pressed_b:
        a += int(pressed_a)
        b += int(pressed_b)
        text = 'A: %d   B: %d' % (a, b)
        counter.setText(text)
        print(text)
    time.sleep_ms(20)
