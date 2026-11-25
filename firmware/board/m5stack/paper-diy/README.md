# M5Stack PaperDIY

Register the K159 Assembly Kit or K159-B Complete Kit as `m5stack/paper-diy`. Both use the same 960 × 540 grayscale display and 16 MiB flash. The runtime uses the M5Stack `M5STACK_PaperDIY` target. [Official demo](https://github.com/m5stack/M5PaperDIY-UserDemo)

Follow the [USB setup and OTA procedure](../../../../README.md) using the PaperDIY kit. First-time installation erases flash; back up existing data before installation. After provisioning, verify the app reports `ESPHUB2/micropython/m5stack/paper-diy`.

Install [main.py](main.py). Check for the PaperDIY greeting, Wi-Fi signal and battery voltage. Press A, B, A. Both the screen and the app log should show `A: 2   B: 1`. Check refresh quality and hardware shutdown on the device.
