"""Host protocol test using a real TLS peer behind a 20-byte GATT transport."""

import json
import os
import ssl
import unittest

from types import SimpleNamespace
from unittest.mock import patch

from macos import SERVICE, Channel, direct_session, unwrap


class Device:
    def __init__(self, psk):
        config = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        config.minimum_version = config.maximum_version = ssl.TLSVersion.TLSv1_3
        config.num_tickets = 0
        config.set_psk_server_callback(
            lambda identity: psk if identity == "wifi" else b""
        )
        self.rx, self.tx = ssl.MemoryBIO(), ssl.MemoryBIO()
        self.tls = config.wrap_bio(self.rx, self.tx, server_side=True)
        self.ready = False
        self.requests = []
        self.line = bytearray()
        self.wire = bytearray()

    def advance(self):
        try:
            if not self.ready:
                self.tls.do_handshake()
                self.ready = True
            self.line.extend(self.tls.read(4096))
            if self.line.endswith(b"\n"):
                self.requests.append(json.loads(self.line))
                self.line.clear()
                self.tls.write(b'{"ssids":["lab"]}\n')
        except ssl.SSLWantReadError:
            pass

    async def write_gatt_char(self, uuid, data, response):
        assert len(data) <= 20 and response
        self.wire.extend(data)
        self.rx.write(data)
        self.advance()

    async def read_gatt_char(self, uuid):
        return self.tx.read(20)


class HostTests(unittest.IsolatedAsyncioTestCase):
    async def test_fragmented_handshake_and_scan(self):
        psk = os.urandom(32)
        device = Device(psk)
        host = Channel(device, psk)
        await host.run(host.tls.do_handshake)
        self.assertEqual(host.tls.version(), "TLSv1.3")
        self.assertEqual(await host.request("wifi.scan"), {"ssids": ["lab"]})
        self.assertEqual(device.requests, [{"op": "wifi.scan"}])
        self.assertNotIn(b"wifi.scan", device.wire)

    async def test_wrong_credential(self):
        device = Device(os.urandom(32))
        host = Channel(device, os.urandom(32))
        with self.assertRaises(ssl.SSLError):
            await host.run(host.tls.do_handshake)
        self.assertEqual(device.requests, [])

    async def test_direct_discovery_and_reconnect(self):
        psk = os.urandom(32)
        devices = [SimpleNamespace(address=name) for name in ("neighbor", "target")]
        closed = []
        async def find(predicate, *, timeout, service_uuids):
            self.assertEqual(service_uuids, [SERVICE])
            return next((d for d in devices if predicate(d, SimpleNamespace(service_uuids=[SERVICE]))), None)

        class Client(Device):
            def __init__(self, device):
                super().__init__(psk if device.address == "target" else os.urandom(32))
                self.address = device.address

            async def __aenter__(self):
                return self

            async def __aexit__(self, *_):
                closed.append(self.address)

        with patch("bleak.BleakClient", Client), patch("bleak.BleakScanner.find_device_by_filter", find):
            for _ in range(2):
                async with direct_session({"psk": psk.hex()}) as session:
                    self.assertEqual(await session.request("wifi.scan"), {"ssids": ["lab"]})
        self.assertEqual(closed, ["neighbor", "target"] * 2)

    def test_cli_envelopes(self):
        credential = {"psk": "example"}
        self.assertEqual(
            unwrap(
                {
                    "result": {
                        "content": [{"type": "text", "text": json.dumps(credential)}]
                    }
                }
            ),
            credential,
        )
