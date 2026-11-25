import M5
import okou
import time
from hardware import MatrixKeyboard

M5.begin()
assert M5.getBoard() == M5.BOARD.M5CardputerADV, 'Expected Cardputer-Adv'
lcd = M5.Lcd
lcd.setRotation(1)
if lcd.width() < lcd.height():
    lcd.setRotation(0)
lcd.setBrightness(160)
lcd.fillScreen(0x000000)
lcd.setTextColor(0xffffff, 0x000000)
lcd.setTextSize(2)
lcd.drawString('Cardputer-Adv', 8, 8)
lcd.drawString('Powered by ESPHub.ai', 0, 36)
lcd.setTextSize(1)
rssi = okou.wifi_rssi()
lcd.drawString('Wi-Fi %s  Battery %s%%' % (rssi, M5.Power.getBatteryLevel()), 8, 72)
keyboard = MatrixKeyboard()
lcd.drawString('Press a key', 8, 98)
print('ESP Hub hello drawn: m5stack/cardputer-adv', lcd.width(), lcd.height())
while True:
    M5.update()
    if keyboard.is_pressed():
        key = keyboard.get_key()
        lcd.fillRect(8, 98, 218, 24, 0x000000)
        lcd.drawString('Key: %s' % (chr(key) if 32 <= key < 127 else key), 8, 98)
        print('Keyboard event:', key)
    time.sleep_ms(20)
