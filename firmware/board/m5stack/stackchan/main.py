import time

import M5
import okou
from hardware.stackchan import StackChan

M5.begin()
assert M5.getBoard() == M5.BOARD.M5StackChan, 'Expected StackChan'
lcd = M5.Lcd
lcd.setBrightness(160)
lcd.fillScreen(0)
lcd.setTextColor(0xffffff, 0)
lcd.setTextSize(2)
lcd.drawString('Hello, M5Stack StackChan', 8, 12)
lcd.drawString('Powered by ESPHub.ai', 8, 48)
body = StackChan(i2c=1, uart=1)
rssi = okou.wifi_rssi()
wifi = 'offline' if rssi is None else '%d dBm' % rssi
lcd.drawString('Wi-Fi: ' + wifi, 8, 92)
lcd.drawString('Battery: %.2f V' % body.get_battery_voltage(), 8, 124)
lcd.drawString('Touch the base', 8, 170)
print('ESP Hub hello drawn: m5stack/stackchan')
colors = (0x33cc00, 0x00cccc, 0x000099)
active = [False, False, False]
while True:
    M5.update()
    touch = body.get_touch()
    for zone in range(3):
        pressed = bool(touch[zone])
        if pressed != active[zone]:
            active[zone] = pressed
            for strip in (0, 1):
                for led in (2 * zone, 2 * zone + 1):
                    body.set_rgb_color(strip, led, colors[zone] if pressed else 0)
            print('Touch', zone + 1, pressed)
    time.sleep_ms(20)
