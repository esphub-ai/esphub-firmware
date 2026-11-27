# Atom Switch

Disconnect power and replace the K115 HDMI/FPGA base with K042. GPIO22 and GPIO19 control the two active-high outputs. Both start low. The single GPIO27 pixel fades cyan before the switch 1 window and purple before the switch 2 window. During each rising green or red window, press GPIO39 to toggle just that output. Green indicates low, and red indicates high.

- [K042 pinout and schematic](https://docs.m5stack.com/en/atom/atomhub_switch#schematics)
