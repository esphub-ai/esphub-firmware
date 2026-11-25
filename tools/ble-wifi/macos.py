#!/usr/bin/env python3
"""ESP Hub Wi-Fi provisioning on macOS. Requires Python >=3.13 and bleak."""

import argparse
import asyncio
import contextlib
import getpass
import json
import ssl
import subprocess
import sys
import uuid

SERVICE = "a2eefb31-6c36-481f-ae10-7724fa912101"
CHANNEL = "a2eefb31-6c36-481f-ae10-7724fa912102"


class Channel:
    """TLS MemoryBIO over ordered, acknowledged GATT writes and bounded reads."""

    def __init__(self, ble, psk):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        # Empty trust store: a certificate-only peer cannot substitute for the PSK.
        context.verify_mode = ssl.CERT_REQUIRED
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_3
        context.set_psk_client_callback(lambda hint: ("wifi", psk))
        self.incoming = ssl.MemoryBIO()
        self.outgoing = ssl.MemoryBIO()
        self.tls = context.wrap_bio(self.incoming, self.outgoing)
        self.ble = ble

    async def flush(self):
        while self.outgoing.pending:
            await self.ble.write_gatt_char(
                CHANNEL, self.outgoing.read(20), response=True
            )

    async def run(self, action):
        while True:
            try:
                result = action()
            except ssl.SSLWantReadError:
                await self.flush()
                chunk = await self.ble.read_gatt_char(CHANNEL)
                if chunk:
                    self.incoming.write(chunk)
                else:
                    await asyncio.sleep(0.02)
            else:
                await self.flush()
                return result

    async def request(self, op, **args):
        data = (json.dumps({"op": op, **args}, ensure_ascii=False) + "\n").encode()
        # One bounded application record per request.
        async with asyncio.timeout(45):
            if await self.run(lambda: self.tls.write(data)) != len(data):
                raise RuntimeError("Incomplete TLS write")
            response = bytearray()
            while not response.endswith(b"\n"):
                piece = await self.run(lambda: self.tls.read(4096))
                if not piece or len(response) + len(piece) > 8192:
                    raise RuntimeError("Invalid device response")
                response.extend(piece)
        result = json.loads(response)
        if "error" in result:
            raise RuntimeError(result["error"])
        return result


@contextlib.asynccontextmanager
async def direct_session(credential):
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError

    psk = bytes.fromhex(credential["psk"])
    if len(psk) != 32:
        raise ValueError("Invalid credential")
    tried = set()
    deadline = asyncio.get_running_loop().time() + 90
    while (remaining := deadline - asyncio.get_running_loop().time()) > 0:
        device = await BleakScanner.find_device_by_filter(
            lambda device, adv: SERVICE in adv.service_uuids and device.address not in tried,
            timeout=remaining,
            service_uuids=[SERVICE],
        )
        if device is None:
            break
        tried.add(device.address)
        async with contextlib.AsyncExitStack() as stack:
            try:
                async with asyncio.timeout(45):
                    ble = await stack.enter_async_context(BleakClient(device))
                    session = Channel(ble, psk)
                    await session.run(session.tls.do_handshake)
            except (BleakError, OSError, TimeoutError):
                # Nearby boards share the service UUID. Only the target can authenticate.
                continue
            yield session
            return
    raise RuntimeError("No board authenticated. Check bootloader mode and obtain a fresh credential.")


async def provision(credential):
    async with direct_session(credential) as session:
        result = await session.request("wifi.scan")
        with open("/dev/tty", "r") as keyboard, open("/dev/tty", "w") as terminal:
            for i, name in enumerate(result["ssids"], 1):
                print(f"{i}. {name!r}", file=terminal)
            print(
                "Network number, or enter a hidden SSID: ",
                end="",
                file=terminal,
                flush=True,
            )
            choice = keyboard.readline().rstrip("\r\n")
            listed = choice.isdecimal() and 0 < int(choice) <= len(result["ssids"])
            ssid = result["ssids"][int(choice) - 1] if listed else choice
            while True:
                password = getpass.getpass(
                    "Wi-Fi password (empty for open network): ", stream=terminal
                )
                await session.request(
                    "wifi.join", ssid=ssid, password=password, hidden=not listed
                )
                async with asyncio.timeout(90):
                    while True:
                        status = await session.request("wifi.status")
                        if (
                            status["state"] == "saved"
                            and status["connected"] == ssid
                        ):
                            print(
                                "Device saved the network and obtained an IP on the selected Wi-Fi."
                            )
                            return
                        if status["state"] == "failed":
                            print(
                                "The board could not join or save the network. Re-enter the password or press Ctrl-C.",
                                file=terminal,
                            )
                            break
                        await asyncio.sleep(1)


def unwrap(payload):
    # Accept raw operation JSON, native MCP content and Okou's result wrapper.
    if isinstance(payload, dict):
        if "psk" in payload:
            return payload
        for field in ("result", "data", "structuredContent", "content"):
            if field in payload:
                value = unwrap(payload[field])
                if value:
                    return value
        if payload.get("type") == "text":
            return unwrap(json.loads(payload["text"]))
    if isinstance(payload, list):
        for item in payload:
            value = unwrap(item)
            if value:
                return value


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--device",
        type=uuid.UUID,
        help="request the credential using the local Okou CLI (otherwise read JSON on stdin)",
    )
    args = parser.parse_args()
    if args.device:
        reply = subprocess.run(
            [
                "okou",
                "mcp",
                "call",
                "esphub-mcp",
                "wifi.ble_credential",
                "--input",
                json.dumps({"device_id": str(args.device)}),
                "--json",
            ],
            stdout=subprocess.PIPE,
            check=True,
        )
        payload = json.loads(reply.stdout)
    else:
        payload = json.load(sys.stdin)
    credential = unwrap(payload)
    if not credential:
        sys.exit("Missing BLE credential in MCP response")
    try:
        asyncio.run(provision(credential))
    except TimeoutError:
        sys.exit(
            "Timed out waiting for the device. Verify its connection before retrying."
        )
