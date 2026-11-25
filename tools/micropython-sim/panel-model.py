"""Model inputs and assert the visible results of the five original screen demos."""

import sys


class End(Exception):
    pass


class Keyboard:
    def is_pressed(self):
        return tick == 2

    def get_key(self):
        return 75


class Stack:
    def __init__(self, i2c, uart):
        assert i2c == 1 and uart == 1

    def get_battery_voltage(self):
        return 3.85

    def get_touch(self):
        return (tick == 2, False, False)

    def set_rgb_color(self, strip, index, color):
        leds.append((strip, index, color))


class Hardware:
    MatrixKeyboard = Keyboard
    StackChan = Stack


sys.modules["hardware"] = Hardware
sys.modules["hardware.stackchan"] = Hardware


class Power:
    def getBatteryLevel(self):
        return 75

    def getBatteryVoltage(self):
        return 3850


class Screen:
    class EPDMode:
        EPD_TEXT = 1
        EPD_FASTEST = 2

    def width(self):
        return 320

    def height(self):
        return 240

    def setRotation(self, rotation):
        pass

    def setBrightness(self, v):
        pass

    def fillScreen(self, v):
        pass

    def fillRect(self, *args):
        pass

    def setTextColor(self, *args):
        pass

    def setTextSize(self, v):
        pass

    def setFont(self, v):
        pass

    def setEpdMode(self, v):
        pass

    def drawString(self, text, *args):
        drawn.append(text)

    def textWidth(self, text):
        return len(text) * 8

    def newCanvas(self, *args):
        return self

    def push(self, x, y):
        assert (x, y) == (0, 0)

    def delete(self):
        pass


class Label:
    def __init__(self, text, *args):
        drawn.append(text)

    def setText(self, text):
        drawn.append(text)


class Widgets(Screen):
    class FONTS:
        Montserrat16 = 16
        Montserrat24 = 24

    Label = Label


class Button:
    def __init__(self, frame):
        self.frame = frame

    def wasPressed(self):
        return tick == self.frame


class Panel:
    class BOARD:
        M5StackChan = "m5stack/stackchan"
        M5PaperDIY = "m5stack/paper-diy"
        M5CardputerADV = "m5stack/cardputer-adv"

    def begin(self):
        pass

    def getBoard(self):
        return board

    def update(self):
        global tick
        tick += 1
        if tick > 4:
            if "qa_finish" in globals():
                verify()
                qa_finish("screen and board input assertions passed")
            raise End()


Panel.Lcd = Panel.Display = Screen()
Panel.Widgets = Widgets()
Panel.Power = Power()
Panel.BtnA, Panel.BtnB = Button(1), Button(3)
sys.modules["M5"] = Panel()
lines, drawn, leds = [], [], []
tick = 0


def verify():
    assert any("ESP Hub hello drawn:" in line for line in lines)
    if board == "m5stack/stackchan":
        assert "Touch 1 True" in lines and "Touch 1 False" in lines and len(leds) == 8
    elif board == "m5stack/cardputer-adv":
        assert "Keyboard event: 75" in lines and "Key: K" in drawn
    elif board == "m5stack/paper-diy":
        assert drawn[-1] == "A: 1   B: 1"
    else:
        assert any("Battery 75%" in s for s in drawn)


if "qa_finish" not in globals():

    def capture(*args):
        lines.append(" ".join(str(item) for item in args))

    try:
        exec(main, {"__name__": "__main__", "print": capture})
    except End:
        pass
    verify()
