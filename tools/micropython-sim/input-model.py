"""Drive an unchanged board example with deterministic panel input under MicroPython."""

import sys

native = globals().get("native_m5")


class Finished(Exception):
    pass


class Button:
    def __init__(self, bit):
        self.bit = bit

    def wasPressed(self):
        return bool(buttons & self.bit and not previous & self.bit)

    def wasClicked(self):
        return bool(previous & self.bit and not buttons & self.bit)


class Touch:
    @staticmethod
    def getCount():
        return int(point is not None)

    @staticmethod
    def getX():
        assert point is not None
        return point[0]

    @staticmethod
    def getY():
        assert point is not None
        return point[1]


class Label:
    def __init__(self, text, x, y, scale, fg, bg, font):
        assert 0 <= x < width and 0 <= y < height and fg != bg
        if native:
            self.label = native.Widgets.Label(text, x, y, scale, fg, bg, font)
        self.y = y
        screen[y] = text

    def setText(self, text):
        if native:
            self.label.setText(text)
        screen[self.y] = text
        changes.append(text)


class Widgets:
    class FONTS:
        Montserrat14 = 14
        Montserrat24 = 24

    if native:
        FONTS = native.Widgets.FONTS

    Label = Label

    @staticmethod
    def fillScreen(color):
        if native:
            native.Widgets.fillScreen(color)
        screen.clear()


class Panel:
    Widgets = Widgets
    Touch = Touch
    BtnA = Button(1)
    BtnB = Button(2)

    @staticmethod
    def begin():
        global started
        if native:
            native.begin()
        started = True

    @staticmethod
    def update():
        global tick, buttons, previous, point
        assert started
        if native:
            native.update()
        tick += 1
        if tick == len(frames):
            if "qa_finish" in globals():
                verify()
                qa_finish(repr(changes))
            raise Finished()
        previous, buttons = buttons, keys[tick]
        point = frames[tick]


# Two complete A clicks, one complete B click and two distinct touches.
keys = [0, 1, 1, 0, 2, 0, 1, 0]
frames = [None, (22, 31), (22, 31), (28, 33), None, (90, 78), (90, 78), None]
cases = {
    "m5stack/sticks3": (135, 240, "StickS3", ["A: 1  B: 0", "A: 1  B: 1", "A: 2  B: 1"]),
    "m5stack/atoms3r": (128, 128, "AtomS3R", ["Presses: 1", "Presses: 2"]),
    "m5stack/cores3": (320, 240, "CoreS3", ["X: 22  Y: 31", "X: 28  Y: 33", "X: 90  Y: 78"]),
    "m5stack/papers3": (540, 960, "PaperS3", ["Page 2", "Page 3"]),
}
board = globals().get("fixture_board")
if board is None:
    with open("case.txt") as f:
        board = f.read().strip()
width, height, title, expected = cases[board]
screen, changes = {}, []
started, tick, buttons, previous, point = False, -1, 0, 0, None
sys.modules["M5"] = Panel


def verify():
    assert started and screen[6] == title, (board, screen)
    assert changes == expected, (board, changes, expected)


if "qa_finish" not in globals():
    with open("main.py") as f:
        source = f.read()
    try:
        exec(source, {"__name__": "__main__"})
    except Finished:
        pass
    verify()
    print("PASS", board, changes)
