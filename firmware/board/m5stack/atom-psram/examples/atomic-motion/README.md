# Atom Motion

Disconnect power and replace the K115 HDMI/FPGA base with the original STM32 Atom Motion base. SDA is GPIO25 and SCL is GPIO21. The accessory uses address 0x38.

The example sends 180 and 0 to all four servo channels and prints the command values read back from the accessory. Press the GPIO39 button to change the direction used in the next motor phase. GPIO27 shows red for +100 and blue for −100 after successful writes to both motor channels. The commands can drive attached motors and servos as soon as the firmware starts.

- [K115 pin map](https://docs.m5stack.com/en/atom/Atom_Display)
- [Atom Motion schematic](https://docs.m5stack.com/en/atom/atom_motion#schematics)
