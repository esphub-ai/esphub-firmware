"""Run the kit's original main.py under MicroPython with deterministic board inputs."""

import gc, sys

with open("case.txt") as f:
    board = f.read().strip()
with open("model.txt") as f:
    model, size = f.read().strip().split("\n")
with open("main.py") as f:
    main = f.read()
if board in ("m5stack/sticks3", "m5stack/atoms3r", "m5stack/cores3", "m5stack/papers3"):
    with open("input-model.py") as f:
        exec(f.read(), {})
    sys.exit(0)


class Partition:
    TYPE_DATA = 1

    @staticmethod
    def find(kind, label):
        assert kind == Partition.TYPE_DATA and label == "data"
        return [Partition()]

    def info(self):
        return (1, 131, 0, int(size), "data", False)


class ESP:
    pass


ESP.Partition = Partition
sys.modules["esp32"] = ESP


class Identity:
    machine = model


class OS:
    @staticmethod
    def uname():
        return Identity()


sys.modules["os"] = OS


class Hub:
    @staticmethod
    def wifi_rssi():
        return signal


sys.modules["okou"] = Hub


class Time:
    @staticmethod
    def sleep_ms(ms):
        assert ms >= 0


sys.modules["time"] = Time

if board in ("m5stack/stopwatch", "m5stack/paper-color", "m5stack/stackchan", "m5stack/cardputer-adv", "m5stack/paper-diy"):
    for signal in (None, -57):
        with open("panel-model.py") as f:
            exec(f.read(), {"main": main, "board": board})
else:
    for signal in (None, -57):
        lines = []

        def capture(*args):
            lines.append(args)

        exec(main, {"__name__": "__main__", "print": capture})
        assert len(lines) == 4
        assert lines[0] == ("Board:", model)
        assert lines[1] == ("Data partition:", int(size), "bytes")
        assert lines[2][0] == "Free heap:" and lines[2][1] > 0
        assert lines[3] == ("Wi-Fi RSSI:", "offline" if signal is None else "-57 dBm")
print("PASS", board)
