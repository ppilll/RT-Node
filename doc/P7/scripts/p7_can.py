#!/usr/bin/env python3
"""P7 SocketCAN Stage0 recorder (Python standard library only).

Default load sends ONLY 0x183 heartbeat. --mode smoke explicitly sends the
approved Stage0 MODE/CONTROL/STOP protocol sequence. Never configures can0.
Successful send/loopback is not MCU reception. No flash, unlock or reset.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import select
import socket
import struct
import subprocess
import time

from p7_uart import positive, next_slot

FRAME = struct.Struct('=IB3x8s')


def heartbeat(counter):
    return struct.pack('<BBH4x', 1, 0, counter & 0xffff)


def status_frame(can_id, dlc, data, msg_flags=0):
    # MSG_DONTROUTE marks locally generated traffic, including another local
    # socket. CAN_RAW_RECV_OWN_MSGS=0 additionally excludes our own socket.
    if msg_flags & socket.MSG_DONTROUTE or can_id != 0x280 or dlc != 8 or data[0] != 1:
        return None
    _, state, reason, flags, epoch, action = struct.unpack('<BBBBHH', data)
    if state > 7 or flags & 0xc0:
        raise ValueError('invalid 0x280 STATUS')
    return dict(state=state, reason=reason, flags=flags, epoch=epoch, action=action)


def bus_state(interface):
    proc = subprocess.run(['ip', '-details', '-statistics', '-json', 'link', 'show', 'dev', interface],
                          capture_output=True, text=True, timeout=5, check=True)
    return json.loads(proc.stdout)


def check_bus(state):
    info = state[0]
    data = info.get('linkinfo', {}).get('info_data', {})
    if 'UP' not in info.get('flags', []) or data.get('state') in ('BUS-OFF', 'STOPPED', 'SLEEPING'):
        raise RuntimeError('CAN interface not UP/error active; inspect ip link')
    if data.get('bittiming', {}).get('bitrate') != 500000 or 'FD' in data.get('ctrlmode', []):
        raise RuntimeError('requires Classical CAN 500000 bit/s; configure manually')


class Link:
    def __init__(self, args, report, writer, stream):
        self.args, self.report, self.writer, self.stream = args, report, writer, stream
        self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        try:
            self.sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, struct.pack('=II', 0x280, 0xe00007ff))
            self.sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_RECV_OWN_MSGS, 0)
            self.sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_ERR_FILTER, struct.pack('=I', 0x1fffffff))
            self.sock.bind((args.interface,))
            self.sock.setblocking(False)
        except BaseException:
            self.sock.close()
            raise

    def log(self, kind, **details):
        self.writer.writerow(dict(monotonic=time.monotonic(), utc=datetime.now(timezone.utc).isoformat(),
                                  kind=kind, details=json.dumps(details)))
        self.stream.flush()

    def send(self, can_id, data):
        self.report['planned'] += 1
        self.log('TX_PLAN', can_id=can_id, data=data.hex())
        try:
            size = self.sock.send(FRAME.pack(can_id, 8, data))
            if size != FRAME.size:
                raise OSError('partial CAN send')
        except OSError as exc:
            self.report['failed'] += 1
            self.log('TX_ERROR', error=repr(exc))
            raise  # Preserve exception; never retry/accelerate after bus errors.
        now = time.monotonic()
        previous = self.report.get('last_send')
        if previous is not None:
            gap = now - previous
            self.report['interval_min'] = min(self.report.get('interval_min', gap), gap)
            self.report['interval_max'] = max(self.report.get('interval_max', gap), gap)
        self.report.setdefault('first_send', now)
        self.report['last_send'] = now
        self.report['successful'] += 1
        self.log('TX_OK', can_id=can_id, data=data.hex())

    def receive(self, timeout):
        if not select.select([self.sock], [], [], max(0, timeout))[0]:
            return None
        raw, _, msg_flags, _ = self.sock.recvmsg(FRAME.size)
        if len(raw) != FRAME.size:
            raise ValueError('truncated CAN frame')
        can_id, dlc, data = FRAME.unpack(raw)
        self.log('RX', can_id=can_id, dlc=dlc, data=data.hex(), msg_flags=msg_flags)
        if can_id & 0x20000000:
            self.report['bus_error_frames'] += 1
            raise RuntimeError('SocketCAN error frame; stopping load')
        status = status_frame(can_id, dlc, data, msg_flags)
        if status:
            self.report['external_status_frames'] += 1
            self.report['last_status'] = status
            if status['flags'] & 0x10 or status['state'] == 7:
                raise RuntimeError('MCU bus-off or Control FAULT')
        return status

    def wait(self, predicate=lambda s: True, seconds=2):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            status = self.receive(min(0.05, end - time.monotonic()))
            if status and predicate(status):
                return status
        raise TimeoutError('no matching external MCU STATUS')


def smoke(link, seconds):
    initial = link.wait()
    if initial['state'] != 0 or initial['flags'] & 1:
        raise RuntimeError('smoke requires verified Stage0 DISABLED; do not auto-disarm an unknown session')
    action = (initial['action'] + 1) & 0xffff
    try:
        link.send(0x181, struct.pack('<BBHHBB', 1, 2, action, initial['epoch'], 0, 0))
        reply = link.wait(lambda s: s['action'] == action)
        if not reply['flags'] & 0x20:
            raise RuntimeError('MODE refused: policy/KEY0/action gate; do not clear lock or retry')
        if reply['state'] != 4 or reply['flags'] & 1:
            raise RuntimeError('Stage0 WAIT_REMOTE/no-output not observed')
        epoch = reply['epoch']
        start = due = time.monotonic()
        seq = 0
        fresh_seen = False
        while time.monotonic() - start < seconds - 2:
            now = time.monotonic()
            if now >= due:
                seq = (seq + 1) & 0xffff
                link.send(0x180, struct.pack('<HHHH', epoch, seq, 1500 if now - start < 1 else 1510, 1500))
                due, missed = next_slot(due, time.monotonic(), 0.02)
                link.report['skipped_slots'] += missed
            s = link.receive(min(0.02, max(0, due - time.monotonic())))
            if s:
                if s['state'] != 4 or s['flags'] & 1 or s['epoch'] != epoch:
                    raise RuntimeError('unexpected state/epoch/enabled during Stage0 smoke')
                fresh_seen |= bool(s['flags'] & 8)
        if not fresh_seen:
            raise RuntimeError('no fresh REMOTE snapshot observed')
    finally:
        # Explicit smoke only: always attempt final GLOBAL STOP; never claim
        # it was accepted without the external STATUS below.
        action = (action + 1) & 0xffff
        link.send(0x182, struct.pack('<BBH4x', 1, 1, action))
        link.wait(lambda s: s['action'] == action and s['state'] == 0 and not s['flags'] & 1)
        link.report['stop_acknowledged'] = True


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--interface', default='can0')
    p.add_argument('--mode', choices=['load', 'smoke'], default='load')
    p.add_argument('--rate', type=positive, default=50, help='load target frames/s (smoke uses fixed 20ms CONTROL)')
    p.add_argument('--seconds', type=positive, default=600)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--firmware', required=True, help='operator-verified Stage0 build identity')
    p.add_argument('--stage0-confirmed', action='store_true', help='required for explicit MODE/CONTROL/STOP smoke only')
    return p


def main():
    p = parser()
    args = p.parse_args()
    if args.rate > 50 or args.seconds > 3600 or not args.interface or '/' in args.interface:
        p.error('require rate<=50, seconds<=3600 and interface name')
    if args.mode == 'smoke' and (not args.stage0_confirmed or not 3 <= args.seconds <= 30):
        p.error('smoke requires --stage0-confirmed and seconds=3..30; Stage1/2 not authorized')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    report = dict(status='NOT_RUN', hardware_verdict='待人工审核',
                  args={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                  planned=0, successful=0, failed=0, skipped_slots=0, external_status_frames=0,
                  bus_error_frames=0, exceptions=[], started_utc=datetime.now(timezone.utc).isoformat())
    with args.out.open('x', encoding='utf8') as result, args.out.with_suffix('.csv').open('x', newline='', encoding='utf8') as stream:
        writer = csv.DictWriter(stream, fieldnames=['monotonic', 'utc', 'kind', 'details'])
        writer.writeheader()
        link = None
        try:
            report['bus_before'] = bus_state(args.interface)
            check_bus(report['bus_before'])
            link = Link(args, report, writer, stream)
            link.wait()  # Require an external 0x280 before submitting load.
            start = due = time.monotonic()
            report['measurement_start'] = start
            report['measurement_start_utc'] = datetime.now(timezone.utc).isoformat()
            if args.mode == 'smoke':
                smoke(link, args.seconds)
            else:
                counter = 0
                while time.monotonic() - start < args.seconds:
                    now = time.monotonic()
                    if now >= due:
                        link.send(0x183, heartbeat(counter))
                        counter = (counter + 1) & 0xffff
                        due, missed = next_slot(due, time.monotonic(), 1 / args.rate)
                        report['skipped_slots'] += missed
                    link.receive(min(0.02, max(0, due - time.monotonic())))
            report['measurement_end'] = time.monotonic()
            report['measurement_end_utc'] = datetime.now(timezone.utc).isoformat()
            report['measurement_seconds'] = report['measurement_end'] - start
            report['nominal_slots'] = math.ceil(args.seconds * args.rate) if args.mode == 'load' else None
            report['actual_send_per_second'] = report['successful'] / max(report['measurement_seconds'], 1e-9)
            report['status'] = 'CAPTURED'
        except (Exception, KeyboardInterrupt) as exc:
            report['status'] = 'FAIL'
            report['exceptions'].append(dict(type=type(exc).__name__, message=str(exc), monotonic=time.monotonic(),
                                             context=repr(exc.__context__) if exc.__context__ else None))
        finally:
            if link is not None:
                link.sock.close()
            try:
                report['bus_after'] = bus_state(args.interface)
                check_bus(report['bus_after'])
                report['bus_error_deltas'] = {}
                before = report.get('bus_before', [{}])[0].get('stats64', {})
                after = report['bus_after'][0].get('stats64', {})
                for direction in ('rx', 'tx'):
                    for field in ('errors', 'dropped'):
                        if field in before.get(direction, {}) and field in after.get(direction, {}):
                            delta = after[direction][field] - before[direction][field]
                            report['bus_error_deltas'][direction + '_' + field] = delta
                            if delta:
                                report['status'] = 'FAIL'
            except Exception as exc:
                report['exceptions'].append(dict(type=type(exc).__name__, message=str(exc)))
                report['status'] = 'FAIL'
            report['ended_utc'] = datetime.now(timezone.utc).isoformat()
            json.dump(report, result, ensure_ascii=False, indent=2)
    print(json.dumps({k: report[k] for k in ('status', 'planned', 'successful', 'failed', 'skipped_slots', 'exceptions')}, ensure_ascii=False))
    return 0 if report['status'] == 'CAPTURED' else 1


if __name__ == '__main__':
    raise SystemExit(main())
