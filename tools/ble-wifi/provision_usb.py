"""Write a devices.create or devices.reissue result over USB, then bind its BLE address."""

import argparse
import json
import re
import subprocess
import sys
import time
import uuid


def result_data(value, field):
    """Extract the operation result from raw JSON, an MCP reply or the Okou CLI."""
    if isinstance(value, dict):
        if value.get("isError") or value.get("error"):
            return None
        if field in value:
            return value
        if value.get("type") == "text":
            return result_data(json.loads(value["text"]), field)
        for key in ("result", "data", "structuredContent", "content"):
            if key in value:
                result = result_data(value[key], field)
                if result is not None:
                    return result
    if isinstance(value, list):
        for item in value:
            result = result_data(item, field)
            if result is not None:
                return result
    return None


def write_registration(port, registration):
    fields = {key: registration[key] for key in ("hub", "token", "ble_secret")}
    if not all(isinstance(value, str) and value for value in fields.values()):
        raise ValueError("Incomplete registration")
    record = b"OKL-PROVISION " + json.dumps(fields, separators=(",", ":")).encode() + b"\n"
    port.reset_input_buffer()
    port.write(record)
    port.flush()
    deadline = time.monotonic() + 15
    line = b""
    while time.monotonic() < deadline:
        line += port.read_until(b"\n", 2048)
        if line.endswith(b"\n"):
            reply = line.rstrip(b"\r\n")
            if reply.startswith(b"OKL-PROVISION-OK "):
                address = json.loads(reply.split(b" ", 1)[1]).get("ble_address")
                if address is None:
                    return None
                if not isinstance(address, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", address):
                    raise ValueError("Invalid Bluetooth address in USB receipt")
                return address.lower()
            if reply.startswith(b"OKL-PROVISION-ERR"):
                raise RuntimeError("Device rejected the USB registration.")
            line = b""
        if len(line) > 4096:
            raise RuntimeError("Unreadable USB console. Check the board before retrying.")
    raise RuntimeError("USB acknowledgement timed out. Check the board before retrying.")


def bind_address(device_id, address):
    args = {"device_id": device_id, "ble_address": address}
    command = ["okou", "mcp", "call", "esphub-mcp", "devices.bind_ble_address",
               "--input", json.dumps(args), "--json"]
    try:
        reply = subprocess.run(command, capture_output=True, check=True, timeout=45)
        bound = result_data(json.loads(reply.stdout), "ble_address")
        if bound and bound.get("id") == device_id and bound["ble_address"] == address:
            return
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    raise RuntimeError(f"USB succeeded. Binding unconfirmed. Retry only devices.bind_ble_address with {json.dumps(args)}.")


def main():
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB serial port on the bootloader. Supply private registration JSON on stdin.")
    args = parser.parse_args()
    registration = result_data(json.load(sys.stdin), "token")
    if not registration:
        raise ValueError("Missing registration")
    device_id = str(uuid.UUID(registration["id"]))
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=5)
    port.dtr = port.rts = False
    port.port = args.port
    with port:
        address = write_registration(port, registration)
    if address:
        bind_address(device_id, address)
        print(f"USB provisioned. Bound {device_id} to {address}.")
    else:
        print("USB provisioned. This firmware did not return a Bluetooth address.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError):
        sys.exit("Invalid registration JSON or USB receipt. Check the registration and device before retrying.")
    except (RuntimeError, OSError) as error:
        sys.exit(str(error))
