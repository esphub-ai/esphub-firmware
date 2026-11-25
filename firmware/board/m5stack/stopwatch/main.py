import M5
import okou
from M5 import Widgets

lcd = M5.Lcd
lcd.setBrightness(160)
w, h = lcd.width(), lcd.height()
canvas = lcd.newCanvas(w, h, 16, True)

def center(text, y, font):
    canvas.setFont(font)
    canvas.drawString(text, (w - canvas.textWidth(text)) // 2, y)

try:
    canvas.fillScreen(0xffffff)
    canvas.setTextColor(0x000000, 0xffffff)
    rssi = okou.wifi_rssi()
    level = M5.Power.getBatteryLevel()
    wifi = '%d dBm' % rssi if rssi is not None else 'offline'
    battery = '%d%%' % level if 0 <= level <= 100 else 'unavailable'
    center('Wi-Fi ' + wifi + '   Battery ' + battery, h // 7, Widgets.FONTS.Montserrat16)
    center('Hello, M5Stack Stopwatch', h // 2 - 28, Widgets.FONTS.Montserrat24)
    center('Powered by ESPHub.ai', h // 2 + 12, Widgets.FONTS.Montserrat24)
    canvas.push(0, 0)
finally:
    canvas.delete()
print('ESP Hub hello drawn: m5stack/stopwatch')
