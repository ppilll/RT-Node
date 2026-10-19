#!/usr/bin/env python3
"""P7 Stage0 read-only UART recorder. No reset/unlock/control commands.

Only one writer may own the port. Every request waits for its reply before
the next request. PONG has no wire ID: host transaction IDs are bookkeeping.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import time

QUERIES = ('HEALTH?', 'COUNT?', 'PERF?', 'PERF2?', 'PERF3?', 'PERF4?', 'CANC?')
EXPECTED = dict(zip(QUERIES, ('HEALTH', 'COUNT', 'P7', 'P7N', 'P7W', 'P7S', 'CAN')))
EXPECTED['PING'] = 'PONG'
KEYS = {'P7': 'abxv', 'P7N': 'ncih', 'P7W': 'tufd', 'P7S': 'mfwl', 'CAN': 'rqed'}


def classify(raw):
    try:
        line = raw.decode('ascii') if isinstance(raw, bytes) else raw
    except UnicodeDecodeError:
        return 'MALFORMED', {}
    if line == 'PONG':
        return 'PONG', {}
    for prefix, keys in KEYS.items():
        m = re.fullmatch(re.escape(prefix) + ''.join(' ' + k + r'=(0|[1-9][0-9]{0,9})' for k in keys), line)
        if m:
            values = [int(x) for x in m.groups()]
            return (prefix, dict(zip(keys, values))) if max(values) <= 0xffffffff else ('MALFORMED', {})
    m = re.fullmatch(r'RX=(0|[1-9][0-9]{0,9}),OVF=(0|[1-9][0-9]{0,9})', line)
    if m:
        values = list(map(int, m.groups()))
        return ('COUNT', dict(zip(('RX', 'OVF'), values))) if max(values) <= 0xffffffff else ('MALFORMED', {})
    m = re.fullmatch(r'HEALTH C=(INIT|RUN|STALL) W=(OFF|START|ON|STARTERR) R=(UNKNOWN|POWER|IWDG|SOFT|PIN|OTHER) L=([01]) P=(AUTO|LOCK|MANUAL|EMPTY|BAD) A=([01]) T=([01])', line)
    if m:
        return 'HEALTH', dict(zip('CWRLPAT', m.groups()))
    if line.startswith('ADC'):
        if line in ('ADC NOT READY', 'ADC ERROR INIT', 'ADC ERROR START', 'ADC ERROR DMA', 'ADC ERROR STALLED'):
            return 'ADC', {}
        match = re.fullmatch(r'ADC: raw=([0-9]{1,10}),avg=([0-9]{1,10}),mv=([0-9]{1,10}),n=([0-9]{1,10}),missed=([0-9]{1,10})', line)
        if match and max(map(int, match.groups())) <= 0xffffffff:
            return 'ADC', {}
        return 'MALFORMED', {}
    if line.startswith('ENV'):
        return 'ENV', {}
    if line == 'ERR' or line.startswith('SAFE '):
        return 'ERR', {}
    return 'MALFORMED' if line.startswith(('P7', 'CAN ', 'RX=', 'HEALTH')) else 'OTHER', {}


class Lines:
    def __init__(self):
        self.pending = bytearray()
        self.discard = False

    def feed(self, chunk):
        rows = []
        for byte in chunk:
            self.pending.append(byte)
            if byte == 10:
                raw = bytes(self.pending[:-1])
                if raw.endswith(b'\r'):
                    raw = raw[:-1]
                rows.append((raw, *classify(raw)) if not self.discard else (raw, 'MALFORMED', {}))
                self.pending.clear()
                self.discard = False
            elif len(self.pending) >= 1024:
                rows.append((bytes(self.pending), 'MALFORMED', {}))
                self.pending.clear()
                self.discard = True
        return rows


def positive(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('must be finite and >0')
    return value


def next_slot(due, now, interval):
    """Skip missed slots; never catch up with back-to-back sends."""
    missed = max(0, int((now - due) // interval))
    return (now + interval if missed else due + interval), missed


class Recorder:
    def __init__(self, args, writer, csvfile, report):
        self.args, self.writer, self.csvfile, self.report = args, writer, csvfile, report
        self.lines = Lines()
        self.serial = None
        self.transaction = 0
        self.latest = {}
        self.desynchronized = False

    def log(self, kind, raw='', **extra):
        row = dict(monotonic=time.monotonic(), utc=datetime.now(timezone.utc).isoformat(),
                   transaction=self.transaction, kind=kind, raw=raw, details=json.dumps(extra))
        self.writer.writerow(row)
        self.csvfile.flush()

    def receive(self):
        rows = self.lines.feed(self.serial.read(max(1, self.serial.in_waiting)))
        for raw, kind, fields in rows:
            self.log(kind, repr(raw), fields=fields)
            self.report['lines'][kind] = self.report['lines'].get(kind, 0) + 1
            if fields:
                previous = self.latest.get(kind)
                if kind == 'P7W' and previous and (fields['u'] - previous['u']) & 0xffffffff > 60000:
                    raise RuntimeError('MCU tick regressed or observation gap >60s; possible reset')
                self.latest[kind] = fields
            if kind in ('ERR', 'MALFORMED'):
                raise RuntimeError('device error or malformed diagnostic; preserve raw CSV')
            if kind == 'HEALTH' and (fields['C'] != 'RUN' or fields['W'] != 'ON' or fields['T'] != '0'):
                raise RuntimeError('health not production RUN/ON/T=0')
            if kind == 'P7S' and fields['f'] != 0:
                raise RuntimeError('Control terminal fault observed')
            if kind == 'P7W' and fields['f'] & 6:
                raise RuntimeError('P7 saturated/invalid observation')
            if kind == 'P7' and (fields['v'] or fields['x'] >= 144000 or
                                 (fields['a'] and fields['a'] < 1368000) or fields['b'] > 1512000):
                raise RuntimeError('Control observation outside P7 limits')
            if kind == 'P7N' and any(fields[k] == 0 for k in 'cih'):
                raise RuntimeError('zero critical stack high-water mark')
            if kind == 'CAN' and any(fields[k] for k in 'qed'):
                raise RuntimeError('CAN receive event observed; stop load and inspect')
            if kind == 'COUNT' and fields['OVF']:
                raise RuntimeError('UART overflow/error event observed')
        return rows

    def request(self, command, split=False):
        if self.desynchronized:
            raise RuntimeError('reply timeout desynchronized serial transactions')
        self.transaction += 1
        self.report['requests_planned'] += 1
        self.log('TX_PLAN', command)
        payload = (command + ('\r\n' if self.transaction % 2 else '\n')).encode('ascii')
        chunks = [payload[:2], payload[2:]] if split else [payload]
        for chunk in chunks:
            size = self.serial.write(chunk)
            self.report['bytes_written'] += size
            if size != len(chunk):
                raise OSError('partial serial write')
            if split:
                time.sleep(0.005)
        self.report['requests_written'] += 1
        if command == 'PING':
            self.report['ping_written'] += 1
        self.log('TX_OK', command, bytes=len(payload))
        deadline = time.monotonic() + self.args.timeout
        while time.monotonic() < deadline:
            matches = 0
            for raw, kind, fields in self.receive():
                if kind == EXPECTED[command]:
                    matches += 1
                elif kind != 'ADC':
                    self.log('UNEXPECTED_REPLY', repr(raw), expected=EXPECTED[command])
                    raise RuntimeError('unexpected/out-of-order reply')
            if matches:
                if matches != 1:
                    raise RuntimeError('duplicate reply in a serial transaction')
                if command == 'PING':
                    self.report['pong_matched'] += 1
                return
        self.report['timeouts'] += 1
        self.desynchronized = True
        self.log('TIMEOUT', command)
        raise TimeoutError(command + ': no matching reply; stopping to avoid late-PONG attribution')

    def snapshot(self):
        for command in QUERIES:
            self.request(command)
        self.report['snapshots'].append(dict(monotonic=time.monotonic(), values={k: dict(v) for k, v in self.latest.items()}))


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True, help='explicit COM5 or /dev/ttyUSB0; no auto discovery/open')
    p.add_argument('--baud', type=int, choices=[115200], default=115200)
    p.add_argument('--mode', choices=['check', 'baseline', 'load', 'boundary'], default='baseline')
    p.add_argument('--seconds', type=positive, default=120)
    p.add_argument('--poll-seconds', type=positive, default=5)
    p.add_argument('--ping-rate', type=positive, default=10)
    p.add_argument('--timeout', type=positive, default=1)
    p.add_argument('--warmup-timeout', type=positive, default=30)
    p.add_argument('--drain-seconds', type=positive, default=2, help='passive receive after timed window, before final snapshot')
    p.add_argument('--firmware', required=True, help='build SHA256/commit and Stage0 identity recorded by operator')
    p.add_argument('--out', type=Path, required=True, help='JSON path; adjacent CSV streamed and flushed')
    return p


def main():
    p = parser()
    args = p.parse_args()
    if args.ping_rate > 10 or args.poll_seconds < 5 or args.seconds > 3600 or args.timeout > 10:
        p.error('ping-rate<=10, poll-seconds>=5, seconds<=3600, timeout<=10 required')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    csvpath = args.out.with_suffix('.csv')
    # Exclusive output creation prevents accidental loss of previous exceptions.
    report = dict(status='NOT_RUN', hardware_verdict='待人工审核', args={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                  started_utc=datetime.now(timezone.utc).isoformat(), lines={}, requests_planned=0,
                  requests_written=0, ping_written=0, pong_matched=0, bytes_written=0,
                  timeouts=0, skipped_ping_slots=0, snapshots=[], exceptions=[])
    with args.out.open('x', encoding='utf8') as resultfile, csvpath.open('x', newline='', encoding='utf8') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=['monotonic', 'utc', 'transaction', 'kind', 'raw', 'details'])
        writer.writeheader()
        rec = Recorder(args, writer, csvfile, report)
        try:
            import serial
            port = serial.Serial(port=None, baudrate=115200, bytesize=8, parity='N', stopbits=1,
                                 timeout=0.02, write_timeout=args.timeout, rtscts=False, dsrdtr=False)
            rec.serial = port
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()  # No reset calls, DTR/RTS kept low. Adapter wiring still requires operator review.
            deadline = time.monotonic() + args.warmup_timeout
            # Drain pre-existing complete lines. Only ADC is unsolicited.
            settle = time.monotonic() + 0.2
            while time.monotonic() < settle:
                for raw, kind, fields in rec.receive():
                    if kind != 'ADC':
                        raise RuntimeError('pre-existing non-ADC reply; verify sole serial writer')
            while True:
                rec.request('PERF2?')
                if rec.latest['P7N']['n'] > 0:
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError('10s P7 window not ready')
                time.sleep(0.5)
            rec.snapshot()
            print('READY: initial CANC/COUNT/HEALTH recorded; timed UART window starts now. '
                  'Start RK load only after this line; retain a post-RK final check if RK finishes later.', flush=True)
            report['measurement_start_utc'] = datetime.now(timezone.utc).isoformat()
            report['measurement_start'] = start = time.monotonic()
            end = start + (0 if args.mode == 'check' else args.seconds)
            ping_due, poll_due = start, start + args.poll_seconds
            while time.monotonic() < end:
                now = time.monotonic()
                if now >= poll_due:
                    rec.snapshot()
                    poll_due, _ = next_slot(poll_due, time.monotonic(), args.poll_seconds)
                elif args.mode in ('load', 'boundary') and now >= ping_due:
                    rec.request('PING', split=args.mode == 'boundary')
                    ping_due, missed = next_slot(ping_due, time.monotonic(), 1 / args.ping_rate)
                    report['skipped_ping_slots'] += missed
                else:
                    for raw, kind, fields in rec.receive():
                        if kind != 'ADC':
                            raise RuntimeError('unsolicited non-ADC reply')
            report['measurement_end'] = time.monotonic()
            report['measurement_end_utc'] = datetime.now(timezone.utc).isoformat()
            report['measurement_seconds'] = report['measurement_end'] - start
            report['ping_slots_nominal'] = (math.ceil(args.seconds * args.ping_rate)
                                             if args.mode in ('load', 'boundary') else 0)
            report['actual_ping_per_second'] = report['ping_written'] / max(report['measurement_seconds'], 1e-9)
            drain = time.monotonic() + args.drain_seconds
            while time.monotonic() < drain:
                for raw, kind, fields in rec.receive():
                    if kind != 'ADC':
                        raise RuntimeError('unexpected drain reply')
            rec.snapshot()
            complete_by = time.monotonic() + args.timeout
            while rec.lines.pending and time.monotonic() < complete_by:
                for raw, kind, fields in rec.receive():
                    if kind != 'ADC':
                        raise RuntimeError('unexpected trailing reply')
            first, last = report['snapshots'][0]['values'], report['snapshots'][-1]['values']
            report['deltas'] = {
                'uart_rx_bytes': (last['COUNT']['RX'] - first['COUNT']['RX']) & 0xffffffff,
                'uart_ovf_events': (last['COUNT']['OVF'] - first['COUNT']['OVF']) & 0xffffffff,
                'can_processed': (last['CAN']['r'] - first['CAN']['r']) & 0xffffffff,
                'period_samples': (last['P7N']['n'] - first['P7N']['n']) & 0xffffffff,
            }
            if args.mode == 'boundary' and report['deltas']['uart_rx_bytes'] <= 256:
                raise RuntimeError('boundary capture did not span >256 RX bytes')
            report['status'] = 'CAPTURED'  # Hardware verdict is never fabricated by collection.
        except (Exception, KeyboardInterrupt) as exc:
            report['status'] = 'FAIL'
            report['exceptions'].append(dict(type=type(exc).__name__, message=str(exc), monotonic=time.monotonic(),
                                             context=repr(exc.__context__) if exc.__context__ else None))
            rec.log('EXCEPTION', repr(exc))
        finally:
            if rec.serial is not None:
                rec.serial.close()
            if rec.lines.pending:
                rec.log('INCOMPLETE', repr(bytes(rec.lines.pending)))
                report['exceptions'].append(dict(type='IncompleteLine', message=repr(bytes(rec.lines.pending))))
                report['status'] = 'FAIL'
            report['last'] = rec.latest
            report['ended_utc'] = datetime.now(timezone.utc).isoformat()
            json.dump(report, resultfile, indent=2, ensure_ascii=False)
    print(json.dumps({k: report[k] for k in ('status', 'ping_written', 'pong_matched', 'timeouts', 'exceptions')}, ensure_ascii=False))
    return 0 if report['status'] == 'CAPTURED' else 1


if __name__ == '__main__':
    raise SystemExit(main())
