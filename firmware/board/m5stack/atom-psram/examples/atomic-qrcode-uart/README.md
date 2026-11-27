# Atom QRCode UART control

Disconnect power and replace the K115 HDMI/FPGA base with K041. UART2 uses 9600 8N1, with host RX on GPIO22 and TX on GPIO19. GPIO23 TRIG stays undriven in this mode.

Startup lights the GPIO27 pixel yellow and selects scanner host mode. Press GPIO39 to start a scan and turn the pixel blue. Each debounced press triggers once, including when held. The console forwards its raw bytes to the scanner with a wake byte and a delay. Scanner ACKs and scan data are copied unchanged to UART0.

- [K041 pin map and manual](https://docs.m5stack.com/en/atom/Atom_QRCode#pinmap)
