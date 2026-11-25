import io
import json
import os
import pty
import select
import subprocess
import sys
import threading
import unittest
from unittest.mock import Mock, patch

from provision_usb import bind_address, main, write_registration


class UsbTests(unittest.TestCase):
    def test_provision_and_bind_over_serial(self):
        registration = {"id": "deedbeef-0000-4000-8000-000000000000", "hub": "api.esphub.ai",
                        "token": "okl_dev_fixture", "ble_secret": "01" * 32}
        address = "02:11:22:aa:bb:cc"
        master, slave = pty.openpty()
        # Exercise the CLI's serial port setup, fragmented receipt and MCP result checks.
        serial_record = []

        def board():
            if select.select([master], [], [], 5)[0]:
                with os.fdopen(os.dup(master), "rb", buffering=0) as reader:
                    serial_record.append(reader.readline())
                os.write(master, b'boot log\nOKL-PROVISION-OK {"ble_address":"02:11:')
                os.write(master, b'22:AA:BB:CC"}\r\n')

        def mcp(command, **kwargs):
            self.assertEqual(command[:5], ["okou", "mcp", "call", "esphub-mcp", "devices.bind_ble_address"])
            self.assertEqual(json.loads(command[6]), {"device_id": registration["id"], "ble_address": address})
            result = {"id": registration["id"], "ble_address": address}
            return subprocess.CompletedProcess(command, 0, stdout=json.dumps({
                "data": {"content": [{"type": "text", "text": json.dumps(result)}]}
            }))

        worker = threading.Thread(target=board)
        worker.start()
        try:
            with patch.object(sys, "argv", ["provision_usb.py", os.ttyname(slave)]), \
                 patch.object(sys, "stdin", io.StringIO(json.dumps(registration))), \
                 patch("provision_usb.subprocess.run", side_effect=mcp) as call:
                main()
                call.assert_called_once()
            payload = serial_record[0].removeprefix(b"OKL-PROVISION ")
            self.assertEqual(json.loads(payload), {k: v for k, v in registration.items() if k != "id"})
        finally:
            worker.join(6)
            os.close(master)
            os.close(slave)

    def test_registration_without_address_and_registration_failure(self):
        for reply, expected in [(b"OKL-PROVISION-OK {}\n", None), (b"OKL-PROVISION-ERR invalid\n", RuntimeError)]:
            port = Mock()
            port.read_until.return_value = reply
            registration = {"hub": "example.org", "token": "test", "ble_secret": "test"}
            if expected:
                with self.assertRaises(expected):
                    write_registration(port, registration)
            else:
                self.assertIsNone(write_registration(port, registration))
            port.write.assert_called_once()

    def test_bind_requires_matching_mcp_receipt(self):
        with patch("provision_usb.subprocess.run") as call:
            call.return_value.stdout = '{"result":{"isError":true}}'
            with self.assertRaisesRegex(RuntimeError, "Retry only devices.bind_ble_address"):
                bind_address("device", "02:11:22:aa:bb:cc")
            call.assert_called_once()
