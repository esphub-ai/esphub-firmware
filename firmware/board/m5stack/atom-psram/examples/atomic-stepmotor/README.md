# Atom StepMotor

Disconnect power and replace the K115 HDMI/FPGA base with Atomic StepMotor Base A132. The DRV8825 uses GPIO22 EN (active low), GPIO23 DIR and GPIO19 STEP. Set the three MODE switches for 1/32 microstepping. Leave GPIO21 reset/sleep and GPIO25 fault under the base's pull-ups.

After the green RGB indicator lights and startup completes, press GPIO39 to move 5,000 full steps forward then back at a nominal 300 RPM with a 200-step motor. The firmware enables the driver during startup.

- [A132 schematic](https://docs.m5stack.com/en/atom/atomic_stepmotor_base#schematics)
