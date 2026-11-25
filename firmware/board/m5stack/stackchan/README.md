# M5Stack StackChan

Register the K151 robot as `m5stack/stackchan`. The runtime uses the M5Stack `M5STACK_StackChan` target and freezes the official Python body driver and its imports. The compact runtime uses the native M5 display bindings. [Official board support package](https://github.com/m5stack/StackChan-BSP)

Follow the [USB setup and OTA procedure](../../../../README.md) using the StackChan kit. First-time installation erases flash. Preserve existing data, including servo calibration, before installation. After provisioning, verify the app reports `ESPHUB2/micropython/m5stack/stackchan`.

Install [main.py](main.py). Verify the screen shows the StackChan greeting, Wi-Fi signal and battery voltage. Touch each base zone and release it. Each zone should light two LEDs on each strip and log its state; releasing it should clear that group. The example leaves servo power off.
