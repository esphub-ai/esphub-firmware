# M5Stack PaperColor factory photo frame

M5Stack's User Demo 1.0.1 has a local photo slideshow and an EzData cloud frame.
The build fetches the pinned upstream source, checks the archive SHA-256 and
applies the board-local changes. The upstream repository uses the MIT license.
See [the pinned source](https://github.com/m5stack/M5PaperColor-UserDemo/tree/884a7b7ebe1d5f1ca55c0dc4864f7708636677ea)
and [the upstream notice](LICENSE.upstream).

The Agent starts before the factory application. Local mode and first-time
setup use the frame's own `PaperColor-XXXXXX` hotspot for photo uploads. The
saved frame mode selects the owner again at boot. Internet mode closes the
hotspot and gives the SDK station its saved networks for EzData and reconnects.
Hold button A for five seconds to reopen setup. The frame migrates a previous
factory station credential into the SDK's saved network list.

When a change will disconnect the browser, the frame acknowledges the request
before switching. Join the selected network and open the displayed
`<device-name>.local`, or connect to the frame hotspot at `192.168.4.1`.
A failed setup join restores the hotspot so the password can be corrected.
An independent Ethernet connection can continue to serve the same web UI.

Build from the repository root with ESP-IDF 5.5.4 active.

```sh
python3 scripts/build-example.py m5stack/paper-color --example factory-demo \
  --version 1.0.1-sdk8-lab --out dist
```

This port is pending board-level validation. The factory keeps its wear-levelled
FAT and SD backends. On the standard EspHub flash layout, the app stores photos
in its own directory on the shared LittleFS volume. It initializes that volume
only if every byte is erased. USB mass storage for the internal LittleFS gallery
and physical SD switching still require integration and verification. RF
association, DHCP to a phone and real EzData delivery require a physical board.
