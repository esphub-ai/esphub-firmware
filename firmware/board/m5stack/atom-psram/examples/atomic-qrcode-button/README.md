# Atom QRCode button control

Disconnect power and replace the K115 HDMI/FPGA base with the K041 Atom QRCode base. The scanner uses TRIG on GPIO23, DONE (DLED) on GPIO33, and UART2 at 9600 8N1 with host RX on GPIO22 and TX on GPIO19.

Hold the GPIO39 button to assert TRIG. The GPIO27 pixel is yellow while DONE is low and green while it is high. Scanner bytes are forwarded as-is to the UART0 console while DONE is high.

- [K115 pin map](https://docs.m5stack.com/en/atom/Atom_Display)
- [K041 pin map and module manual](https://docs.m5stack.com/en/atom/Atom_QRCode#pinmap)
