"""Exercise malformed/repeated records through the production TCP transport.

Only Python's standard library and the standalone native test host are needed.
"""
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

HOST = Path(sys.argv.pop(1)).resolve() if __name__ == "__main__" else None
GRANT = "runtime_item_grant.priority.json"
MESSAGE = "runtime_ap_message.json"


def record(name, value, operation=1):
    payload = value if isinstance(value, bytes) else json.dumps(value, separators=(",", ":")).encode()
    key = name.encode()
    return struct.pack("<BHQI", operation, len(key), 0, len(payload)) + key + payload


def grant(process_id):
    return dict(request_schema="kh3ap.item_delivery.v1", process_id=process_id,
                seed_name="test", slot=1, request_id="batch",
                action_kind="runtime_item_grant_batch", grants=[dict(
                    index=0, kh3_runtime_token="ITEM_POTION",
                    kh3_grant_method="bp_tres_get_item")])


class PacketRegression(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="kh3-packet-regression-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.peer = None
        self.process = subprocess.Popen([str(HOST), str(self.root), "--tcp"],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(self.stop_host)
        endpoint = self.root / "ipc_endpoint.json"
        deadline = time.monotonic() + 5
        while True:
            self.assertIsNone(self.process.poll(), "native host exited during startup")
            try:
                self.endpoint = json.loads(endpoint.read_text(encoding="utf-8"))
                break
            except (FileNotFoundError, PermissionError):
                pass
            if time.monotonic() >= deadline:
                self.fail("native host did not publish its endpoint")
            time.sleep(0.01)
        self.assertEqual(self.process.pid, self.endpoint["pid"])
        self.connect()

    def stop_host(self):
        if self.process.poll() is None:
            try:
                self.send_records([record(MESSAGE, {"stop": True})])
                self.process.wait(timeout=5)
            except (AttributeError, OSError, subprocess.TimeoutExpired):
                self.process.terminate()
                self.process.wait(timeout=5)
        if self.peer is not None:
            self.peer.close()

    def connect(self):
        self.peer = socket.create_connection(("127.0.0.1", self.endpoint["port"]), timeout=3)
        self.send(b"K3P3" + self.endpoint["token"].encode())
        self.assertEqual(b"K3P3", self.receive()[:4])

    def send(self, payload):
        self.peer.sendall(struct.pack("<I", len(payload)) + payload)

    def receive(self):
        def exact(count):
            value = bytearray()
            while len(value) < count:
                chunk = self.peer.recv(count - len(value))
                if not chunk:
                    raise EOFError("native peer disconnected")
                value.extend(chunk)
            return bytes(value)
        size, = struct.unpack("<I", exact(4))
        self.assertLessEqual(size, 8 * 1024 * 1024)
        return exact(size)

    def send_records(self, records):
        self.send(b"K3P3" + struct.pack("<QI", 0, len(records)) + b"".join(records))

    def inspection(self):
        self.send_records([record(MESSAGE, {"inspect_prepared_grant": 1})])
        for _ in range(30):
            packet = self.receive()
            self.assertEqual(b"K3P3", packet[:4])
            count, = struct.unpack_from("<I", packet, 12)
            offset = 16
            for _ in range(count):
                _, key_size, _, value_size = struct.unpack_from("<BHQI", packet, offset)
                offset += 15
                key = packet[offset:offset + key_size].decode()
                offset += key_size
                value = packet[offset:offset + value_size]
                offset += value_size
                if key == "runtime_ap_message_result.json":
                    return json.loads(value)
        self.fail("native host did not return the grant inspection")

    def test_malformed_replacement_cannot_retain_an_earlier_grant(self):
        self.send_records([record(GRANT, grant(42)), record(GRANT, {})])
        self.assertEqual(0, self.inspection()["count"])

    def test_unreached_record_cannot_replace_an_earlier_grant(self):
        self.send_records([
            record(GRANT, grant(42)),
            record("ap_items.json", b"invalid patch", operation=4),
            record(GRANT, grant(99)),
        ])
        with self.assertRaises(EOFError):
            for _ in range(30):
                self.receive()
        self.peer.close()
        self.connect()
        self.assertEqual(42, self.inspection()["process_id"])

    def test_valid_replacement_uses_the_last_record(self):
        self.send_records([record(GRANT, grant(42)), record(GRANT, grant(99))])
        result = self.inspection()
        self.assertEqual(1, result["count"])
        self.assertEqual(99, result["process_id"])


if __name__ == "__main__":
    unittest.main()
