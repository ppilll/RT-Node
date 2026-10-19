"""Offline protocol/scheduling/serial-transaction tests. No device is opened."""
import argparse
import csv
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import p7_uart as uart
import p7_can as can


class FakeSerial:
    def __init__(self, replies):
        self.replies = bytearray(replies)
        self.written = bytearray()
        self.chunk_size = 7

    @property
    def in_waiting(self):
        return min(self.chunk_size, len(self.replies))

    def read(self, n):
        result = bytes(self.replies[:n])
        del self.replies[:n]
        return result

    def write(self, chunk):
        self.written.extend(chunk)
        return len(chunk)


def recorder(replies):
    args = argparse.Namespace(timeout=0.001)
    out = io.StringIO()
    writer = csv.DictWriter(out, fieldnames=['monotonic', 'utc', 'transaction', 'kind', 'raw', 'details'])
    report = dict(lines={}, requests_planned=0, requests_written=0, bytes_written=0,
                  ping_written=0, pong_matched=0, timeouts=0)
    rec = uart.Recorder(args, writer, out, report)
    rec.serial = FakeSerial(replies)
    return rec


class ParserTests(unittest.TestCase):
    def test_all_formats_maximum(self):
        for prefix, keys in uart.KEYS.items():
            line = prefix + ''.join(' ' + k + '=4294967295' for k in keys)
            kind, fields = uart.classify(line)
            self.assertEqual(kind, prefix)
            self.assertEqual(list(fields.values()), [0xffffffff] * 4)
            self.assertEqual(len(line) + 2, 56 if prefix == 'P7' else 57)
            self.assertEqual(uart.classify(line.replace('4294967295', '4294967296'))[0], 'MALFORMED')

    def test_fragmented_interleaving(self):
        raw = (b'ADC: raw=1,avg=1,mv=0,n=1,missed=0\r\nPONG\n'
               b'HEALTH C=RUN W=ON R=POWER L=0 P=AUTO A=1 T=0\r\n'
               b'P7 a=1440000 b=1440000 x=100 v=0\nP7N n=1 c=100 i=100 h=100\r\n'
               b'P7W t=10000 u=11000 f=1 d=0\nP7S m=0 f=0 w=550 l=0\n'
               b'CAN r=1 q=0 e=0 d=0\nRX=4294967295,OVF=0\nENV NOT READY\nERR\n')
        for size in (1, 2, 7, 64, 256, 10000):
            parser = uart.Lines()
            rows = []
            for i in range(0, len(raw), size):
                rows.extend(parser.feed(raw[i:i + size]))
            self.assertEqual([r[1] for r in rows], ['ADC', 'PONG', 'HEALTH', 'P7', 'P7N', 'P7W', 'P7S', 'CAN', 'COUNT', 'ENV', 'ERR'])
            self.assertFalse(parser.pending)

    def test_bad_incomplete_and_long(self):
        for raw in (b'P7 a=-1 b=2 x=3 v=0', b'P7 a=01 b=2 x=3 v=0', b'P7 a=1 b=2 x=3',
                    b'P7 a=1 b=2 x=3 v=0 extra', b'RX=4294967296,OVF=0', b'\xff', b'PONG\rX'):
            self.assertNotEqual(uart.classify(raw)[0], 'PONG')
            self.assertIn(uart.classify(raw)[0], ('MALFORMED', 'OTHER'))
        parser = uart.Lines()
        self.assertEqual(parser.feed(b'PO'), [])
        self.assertEqual(parser.pending, b'PO')
        self.assertEqual(parser.feed(b'NG\r\n')[0][1], 'PONG')
        rows = parser.feed(b'x' * 1100 + b'\nPONG\n')
        self.assertEqual([r[1] for r in rows], ['MALFORMED', 'MALFORMED', 'PONG'])

    def test_serial_matching_and_split(self):
        rec = recorder(b'ADC NOT READY\r\nPONG\r\n')
        rec.request('PING', split=True)
        self.assertEqual(rec.report['ping_written'], 1)
        self.assertEqual(rec.report['pong_matched'], 1)
        self.assertEqual(rec.report['lines']['ADC'], 1)
        self.assertIn(bytes(rec.serial.written), (b'PING\n', b'PING\r\n'))

    def test_timeout_no_late_attribution(self):
        rec = recorder(b'ADC NOT READY\n')
        with self.assertRaises(TimeoutError):
            rec.request('PING')
        self.assertEqual(rec.report['timeouts'], 1)
        rec.serial.replies.extend(b'PONG\n')
        with self.assertRaises(RuntimeError):
            rec.request('PING')
        self.assertEqual(rec.report['ping_written'], 1)

    def test_out_of_order_error_and_health_stop(self):
        for raw in (b'CAN r=1 q=0 e=0 d=0\n', b'ERR\n', b'PONG\nPONG\n',
                    b'HEALTH C=STALL W=ON R=IWDG L=1 P=LOCK A=0 T=0\n',
                    b'P7S m=5 f=1 w=10 l=1\n'):
            rec = recorder(raw)
            # Deliver entire batch for the duplicate case.
            rec.serial.chunk_size = 10000
            with self.assertRaises(RuntimeError):
                rec.request('PING')

    def test_p7_limits_and_tick_regression(self):
        for raw in (b'P7 a=1367999 b=1440000 x=1 v=0\n', b'P7 a=1440000 b=1440000 x=144000 v=0\n',
                    b'P7W t=10000 u=11000 f=5 d=1\n', b'CAN r=1 q=1 e=0 d=0\n', b'RX=100,OVF=1\n'):
            rec = recorder(raw)
            rec.serial.chunk_size = 10000
            with self.assertRaises(RuntimeError):
                rec.receive()
        rec = recorder(b'P7W t=10000 u=20000 f=1 d=0\n')
        rec.serial.chunk_size = 10000
        rec.receive()
        rec.serial.replies.extend(b'P7W t=0 u=20 f=1 d=0\n')
        with self.assertRaises(RuntimeError):
            rec.receive()

    def test_can_format_rollover_and_loopback(self):
        self.assertEqual(can.heartbeat(65535), bytes.fromhex('0100ffff00000000'))
        self.assertEqual(can.heartbeat(65536), bytes.fromhex('0100000000000000'))
        self.assertEqual(can.FRAME.size, 16)
        data = struct.pack('<BBBBHH', 1, 4, 0, 8, 1, 2)
        self.assertEqual(can.status_frame(0x280, 8, data)['epoch'], 1)
        for cid, dlc, flags in ((0x183, 8, 0), (0x280, 7, 0), (0x280, 8, can.socket.MSG_DONTROUTE), (0x80000280, 8, 0)):
            self.assertIsNone(can.status_frame(cid, dlc, data, flags))

    def test_monotonic_no_burst(self):
        due, skipped = uart.next_slot(10, 10.002, .02)
        self.assertAlmostEqual(due, 10.02)
        self.assertEqual(skipped, 0)
        due, skipped = uart.next_slot(10, 10.101, .02)
        self.assertGreaterEqual(due - 10.101, .019999)
        self.assertEqual(skipped, 5)

    def test_can_error_logged_without_retry(self):
        fake = can.Link.__new__(can.Link)
        fake.report = dict(planned=0, successful=0, failed=0)
        fake.log = lambda *args, **kwargs: None
        class BadSocket:
            def send(self, raw):
                raise OSError('simulated bus error')
        fake.sock = BadSocket()
        with self.assertRaises(OSError):
            fake.send(0x183, can.heartbeat(1))
        self.assertEqual(fake.report, dict(planned=1, successful=0, failed=1))

    def test_stage0_smoke_protocol_and_final_stop(self):
        class SmokeLink:
            def __init__(self, reject=False):
                self.now = 0
                self.report = {'skipped_slots': 0}
                self.sent = []
                self.reject = reject

            def wait(self, predicate=lambda s: True):
                if not self.sent:
                    return dict(state=0, flags=0, epoch=5, action=40)
                if self.sent[-1][0] == 0x181:
                    return dict(state=4 if not self.reject else 0, flags=0x20 if not self.reject else 0, epoch=6, action=41)
                return dict(state=0, flags=0x20, epoch=6, action=42)

            def send(self, cid, data):
                self.sent.append((cid, data))

            def receive(self, timeout):
                self.now += .02
                return dict(state=4, flags=8, epoch=6, action=41)

        link = SmokeLink()
        with patch.object(can.time, 'monotonic', side_effect=lambda: link.now):
            can.smoke(link, 5)
        self.assertEqual(link.sent[0], (0x181, struct.pack('<BBHHBB', 1, 2, 41, 5, 0, 0)))
        self.assertEqual(link.sent[-1], (0x182, struct.pack('<BBH4x', 1, 1, 42)))
        self.assertTrue(link.report['stop_acknowledged'])
        controls = [struct.unpack('<HHHH', data) for cid, data in link.sent if cid == 0x180]
        self.assertGreater(len(controls), 1)
        self.assertTrue(all(e == 6 and p in (1500, 1510) and t == 1500 for e, s, p, t in controls))
        link = SmokeLink(reject=True)
        with self.assertRaises(RuntimeError):
            can.smoke(link, 5)
        self.assertEqual(link.sent[-1][0], 0x182)

    def test_help_validation_and_no_device_log(self):
        folder = Path(__file__).resolve().parent
        for script in ('p7_uart.py', 'p7_can.py'):
            cmd = [sys.executable, str(folder / script)]
            self.assertEqual(subprocess.run(cmd + ['--help'], capture_output=True).returncode, 0)
            common = ['--firmware', 'OFFLINE_TEST', '--out', 'unused.json']
            if script == 'p7_uart.py':
                common += ['--port', 'NO_DEVICE']
            for bad in ('0', '-1', 'nan', 'inf'):
                self.assertNotEqual(subprocess.run(cmd + common + ['--seconds', bad], capture_output=True).returncode, 0)
        with tempfile.TemporaryDirectory() as td:
            for script, options in [('p7_uart.py', ['--port', 'P7_NO_DEVICE']),
                                    ('p7_can.py', ['--interface', 'p7_no_device'])]:
                out = Path(td) / (script + '.json')
                proc = subprocess.run([sys.executable, str(folder / script), *options,
                                       '--firmware', 'OFFLINE_TEST', '--out', str(out)], capture_output=True)
                self.assertNotEqual(proc.returncode, 0)
                report = json.loads(out.read_text(encoding='utf8'))
                self.assertEqual(report['status'], 'FAIL')
                self.assertTrue(report['exceptions'])
                self.assertTrue(out.with_suffix('.csv').exists())

    def test_uart_complete_boundary_capture_with_mock_device(self):
        # In-memory serial device and accelerated monotonic clock only.
        # Temporary output is discarded; never hardware evidence.
        clock = [0.0]

        def now():
            clock[0] += .005
            return clock[0]

        class Device:
            def __init__(self, **kwargs):
                self.raw = bytearray()
                self.input = bytearray()
                self.received = 0

            def open(self):
                pass

            def close(self):
                pass

            @property
            def in_waiting(self):
                if not self.raw:
                    self.raw.extend(b'ADC NOT READY\r\n')
                return len(self.raw)

            def read(self, size):
                result = bytes(self.raw[:size]); del self.raw[:size]
                return result

            def write(self, data):
                self.received += len(data)
                self.input.extend(data)
                if self.input.endswith(b'\n'):
                    command = self.input.decode().strip(); self.input.clear()
                    replies = dict(PING='PONG', **{
                        'HEALTH?': 'HEALTH C=RUN W=ON R=POWER L=0 P=AUTO A=1 T=0',
                        'COUNT?': f'RX={self.received},OVF=0',
                        'PERF?': 'P7 a=1440000 b=1440000 x=100 v=0',
                        'PERF2?': 'P7N n=500 c=100 i=100 h=100',
                        'PERF3?': 'P7W t=10000 u=20000 f=1 d=0',
                        'PERF4?': 'P7S m=0 f=0 w=1000 l=0',
                        'CANC?': 'CAN r=0 q=0 e=0 d=0'})
                    self.raw.extend((replies[command] + '\r\n').encode())
                return len(data)

        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / 'simulation.json'
            argv = ['p7_uart.py', '--port', 'MOCK_ONLY', '--firmware', 'SIMULATED_UNIT_TEST',
                    '--mode', 'boundary', '--seconds', '30', '--out', str(out)]
            with patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=Device)}), \
                    patch.object(sys, 'argv', argv), patch.object(uart.time, 'monotonic', side_effect=now), \
                    patch.object(uart.time, 'sleep'), patch('builtins.print'):
                self.assertEqual(uart.main(), 0)
            report = json.loads(out.read_text(encoding='utf8'))
            self.assertEqual(report['ping_written'], report['pong_matched'])
            self.assertGreater(report['deltas']['uart_rx_bytes'], 256)
            self.assertGreater(len(report['snapshots']), 2)


if __name__ == '__main__':
    unittest.main()
