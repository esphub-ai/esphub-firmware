# Atomic CAN Base

Remove the Atom-PSRAM host from the K115 Atom Display HDMI/FPGA base and plug it into Atomic CAN Base A103. GPIO22 drives CA-IS3050G TXD and GPIO19 receives RXD. GPIO39 is the active-low button and GPIO27 drives the single RGB pixel. Connect CAN_H and CAN_L to a terminated CAN bus with another active 500 kbit/s node.

The default build sends standard frames every 80 ms. Build with the C++ definition `ATOMIC_CAN_SENDER=0` to reply to requests instead. On the single pixel, replies for indices 0–24 light it red, 25–27 leave it unchanged, and 28 or higher turn it blue. The button turns the pixel off and restarts the board.

- [Atomic CAN Base](https://docs.m5stack.com/en/atom/Atomic_CAN_Base)
- [Atom Display pin map](https://docs.m5stack.com/en/atom/Atom_Display)
