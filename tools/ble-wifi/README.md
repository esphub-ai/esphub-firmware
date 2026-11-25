# ESP Hub Wi-Fi setup on macOS

Use the bootloader and MicroPython images from the latest USB kit for your board. Register with `devices.create` and flash the kit following its INSTALL.txt. On the local USB port at 115200 baud send one newline-terminated record with the returned values:

```text
OKL-PROVISION {"hub":"api.esphub.ai","token":"<token>","ble_secret":"<ble_secret>"}
```

The setup kit includes `provision_usb.py`. Install `pyserial` and pipe the private JSON result of `devices.create` or `devices.reissue` to `python3 provision_usb.py PORT` after flashing. The helper sends the record, reads the receipt, and calls `devices.bind_ble_address` using the authorized local Okou CLI. With another MCP client, read `OKL-PROVISION-OK {"ble_address":"aa:bb:cc:dd:ee:ff"}` and pass that address and the registration’s `device_id` to the same tool. The device returns `OKL-PROVISION-OK {}` when its Bluetooth address is unavailable. The device can now receive its first Wi-Fi over Bluetooth.

On a Mac with Python 3.13+ (OpenSSL with PSK support), install Bleak in a virtual environment and grant Bluetooth access to the terminal. Run with a locally authorized Okou CLI:

```sh
python3.13 -m venv .venv
.venv/bin/pip install 'bleak>=1,<4'
.venv/bin/python tools/ble-wifi/macos.py --device DEVICE_UUID
```

The host requests `wifi.ble_credential` from MCP. Alternatively, pipe that tool's private JSON response directly to the script on stdin and omit `--device`. Keep all credential responses private. Keep the device in EspHub Bootloader mode. The Mac scans for the service and authenticates the requested board, including when several boards are nearby. After disconnecting you can run the tool again while the bootloader is running. Select the SSID in the terminal and enter its password at the hidden prompt. The device scans and attempts the connection itself. It reports successful association with an IP and saves the network. An unsuccessful attempt preserves the saved list and can be retried. Check `devices.get` separately for hub reachability.

## Wire protocol

The bootloader advertises service `a2eefb31-6c36-481f-ae10-7724fa912101`. The Mac connects and starts a TLS handshake on characteristic `a2eefb31-6c36-481f-ae10-7724fa912102`. MCP returns a single `psk` field containing the 32-byte HMAC-SHA-256 of the ASCII string `esphub/wifi/tls-psk` under the binary bootstrap key, encoded as hex. The handshake deadline is 45 seconds.

TLS bytes use acknowledged writes and reads on the same characteristic, up to 20 bytes at a time. TLS 1.3 uses external PSK identity `wifi`, ephemeral key exchange and AES-128-GCM/SHA-256. The TLS stream carries newline-delimited UTF-8 JSON. A command requires a completed PSK handshake.

The issued credential remains valid until the device's bootstrap key is replaced or erased. Each connection performs a fresh TLS handshake.

## Cardputer Zero GUI

The [EspHub Wi-Fi app for Cardputer Zero](https://github.com/e7h4n/esphub-zero) provides keyboard-driven Bluetooth provisioning. Its repository includes the source, Debian package builder, and installation instructions.
