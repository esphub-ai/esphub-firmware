# esp-bootloader Python runtime on PaperColor: a script, not firmware.
# Update without reflashing:  tools/pyrt.py <device> put papercolor_hello.py main.py && tools/pyrt.py <device> reset
import time
import M5
import okou
from M5 import Widgets

M5.Lcd.setRotation(1)  # landscape, 600 x 400
M5.Lcd.setEpdMode(M5.Lcd.EPDMode.EPD_QUALITY)

c = M5.Lcd.newCanvas(600, 400, 24, True)
c.fillScreen(0xFFFFFF)
c.fillRect(0, 0, 600, 12, 0xFF0000)
c.setTextColor(0x000000, 0xFFFFFF)
c.setFont(Widgets.FONTS.Montserrat48)
c.drawString("Hello from Python", 40, 60)
c.setFont(Widgets.FONTS.EFontCN24)
c.drawString("脚本更新，不用刷机", 40, 150)
c.setFont(Widgets.FONTS.Montserrat24)
c.drawString("battery %d%%" % M5.Power.getBatteryLevel(), 40, 230)
c.drawString("hub %s" % ("online" if okou.connected() else "offline"), 40, 270)
c.push(0, 0)
c.delete()

okou.event("py.hello", '{"screen":"hello"}')
print("hello screen drawn")

n = 0
while True:
    n += 1
    time.sleep(5)
