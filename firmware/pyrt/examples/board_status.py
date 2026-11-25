import gc
import os

import esp32
import okou

# Works over the console on boards with or without a display.
board = os.uname().machine
data = esp32.Partition.find(esp32.Partition.TYPE_DATA, label="data")[0]
print("Board:", board)
print("Data partition:", data.info()[3], "bytes")
print("Free heap:", gc.mem_free(), "bytes")
rssi = okou.wifi_rssi()
print("Wi-Fi RSSI:", "offline" if rssi is None else "%d dBm" % rssi)
