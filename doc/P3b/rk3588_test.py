#!/usr/bin/env python3
"""P3b Stage1 SocketCAN helper. Disconnect servo power AND signals first.

No dependencies. Never configures can0, flashes firmware or runs loaded tests.
STATUS is a snapshot; use only one command sender and serial mode requests.
"""
import argparse
import select
import socket
import struct
import time

FRAME = struct.Struct('=IB3x8s')
STATES = ['DISABLED', 'WAIT_LOCAL', 'PRIMING_LOCAL', 'ACTIVE_LOCAL',
          'WAIT_REMOTE', 'PRIMING_REMOTE', 'ACTIVE_REMOTE', 'FAULT']


class Link:
    def __init__(self, interface, action):
        self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        self.sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER,
                             struct.pack('=II', 0x280, 0xE00007FF))
        self.sock.bind((interface,))
        self.action = action
        self.last_printed = None

    def send(self, can_id, data):
        self.sock.send(FRAME.pack(can_id, 8, data))

    def receive(self, timeout):
        if not select.select([self.sock], [], [], max(0, timeout))[0]:
            return None
        can_id, dlc, data = FRAME.unpack(self.sock.recv(FRAME.size))
        if can_id != 0x280 or dlc != 8 or data[0] != 1:
            return None
        version, state, reason, flags, epoch, action = struct.unpack('<BBBBHH', data)
        if state >= len(STATES) or flags & 0xC0:
            raise RuntimeError('Invalid STATUS: ' + data.hex())
        s = dict(state=state, reason=reason, flags=flags, epoch=epoch, action=action)
        key = (state, reason, flags, epoch, action)
        if key != self.last_printed:
            print(f'{time.monotonic():.3f} 280#{data.hex().upper()} '
                  f'{STATES[state]} epoch={epoch} action={action} '
                  f'reason={reason:02x} enabled={flags & 1} fresh={(flags >> 3) & 1}', flush=True)
            self.last_printed = key
        return s

    def wait(self, predicate=lambda s: True, seconds=2):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            s = self.receive(deadline - time.monotonic())
            if s and predicate(s):
                return s
        raise TimeoutError('No matching STM32 STATUS; inspect candump/bus/epoch/action')

    def next_action(self):
        if self.action is None:
            self.action = (self.wait()['action'] + 1) & 0xFFFF
        action = self.action
        self.action = (action + 1) & 0xFFFF
        return action

    def drain(self):
        while self.receive(0) is not None:
            pass

    def mode(self, requested):
        current = self.wait()
        if current['state'] == 7:
            raise RuntimeError('FAULT is latched; normal mode requests cannot restart it')
        action = self.next_action()
        self.drain()
        self.send(0x181, struct.pack('<BBHHBB', 1, requested, action, current['epoch'], 0, 0))
        result = self.wait(lambda s: s['action'] == action)
        if not result['flags'] & 0x20:
            raise RuntimeError('MODE rejected. Use a newer --action after inspecting STATUS; do not replay old commands')
        wanted = [[0], [1, 2, 3], [4, 5, 6]][requested]
        if result['state'] not in wanted:
            raise RuntimeError('MODE acknowledgement/state mismatch')
        return result

    def stop(self):
        action = self.next_action()
        self.drain()
        self.send(0x182, struct.pack('<BBH4x', 1, 1, action))
        return self.wait(lambda s: s['action'] == action and s['state'] in (0, 7) and not s['flags'] & 1)

    def heartbeat(self, counter):
        self.send(0x183, struct.pack('<BBH4x', 1, 0, counter & 0xFFFF))


def stream(link, args):
    initial = link.wait()
    if initial['state'] in (4, 5, 6):
        link.mode(0)  # create a fresh explicit epoch/seq session
    status = link.mode(2)
    epoch = status['epoch']
    print('Sending new seq every 20ms, starting at center; LA must confirm PRIMING/slew.', flush=True)
    started = next_tick = time.monotonic()
    hb_tick = started
    seq = counter = 0
    active_seen = False
    while time.monotonic() - started < args.seconds:
        now = time.monotonic()
        if now >= next_tick:
            seq = (seq + 1) & 0xFFFF
            # Center for the first second, then at most +/-10 us.
            pan, tilt = (1500, 1500) if now - started < 1 else (args.pan, args.tilt)
            link.send(0x180, struct.pack('<HHHH', epoch, seq, pan, tilt))
            next_tick += 0.020
            if next_tick <= now:
                next_tick = now + 0.020  # no catch-up burst
        if now >= hb_tick:
            counter += 1
            link.heartbeat(counter)
            hb_tick = now + 0.100
        s = link.receive(min(0.020, max(0, next_tick - time.monotonic())))
        if s:
            if s['state'] == 7 or s['epoch'] != epoch or s['state'] not in (4, 5, 6):
                raise RuntimeError('Control stopped/changed unexpectedly during stream')
            active_seen |= s['state'] == 6 and bool(s['flags'] & 1)
    if not active_seen:
        raise RuntimeError('ACTIVE_REMOTE/enabled not observed; verify Stage1 firmware (Stage0 cannot output)')
    if args.end == 'local':
        link.mode(1)
        print('LOCAL requested: P6 policy/IMU freshness govern authorization. KEY0 (PE4) is latched STOP; KEY1 only changes OLED page. Do not press KEY0 to start. No further heartbeat is sent.', flush=True)
    elif args.end == 'timeout':
        print('Stopping CONTROL; continuing only HEARTBEAT for 0.6s.', flush=True)
        deadline = time.monotonic() + 0.6
        observed = False
        while time.monotonic() < deadline:
            counter += 1
            link.heartbeat(counter)
            s = link.receive(0.100)
            observed |= bool(s and s['state'] == 0 and s['reason'] == 3 and
                             not s['flags'] & 1 and s['flags'] & 4)
        if not observed:
            raise RuntimeError('REMOTE_TIMEOUT with heartbeat alive not observed')
        print('STATUS timeout observed; confirm PC6/PC7 LOW with LA.', flush=True)
    else:
        link.stop()
        print('STOP acknowledged; confirm PC6/PC7 LOW with LA.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interface', default='can0')
    parser.add_argument('--action', type=int, help='next fresh shared action_id (default last STATUS action + 1)')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('status')
    sub.add_parser('stop')
    mode = sub.add_parser('mode')
    mode.add_argument('mode', choices=['disabled', 'local', 'remote'])
    remote = sub.add_parser('remote')
    remote.add_argument('--seconds', type=float, default=3)
    remote.add_argument('--pan', type=int, choices=range(1490, 1511), default=1510, metavar='1490..1510')
    remote.add_argument('--tilt', type=int, choices=range(1490, 1511), default=1500, metavar='1490..1510')
    remote.add_argument('--end', choices=['stop', 'timeout', 'local'], default='stop')
    args = parser.parse_args()
    if args.command == 'remote' and not 2 <= args.seconds <= 30:
        parser.error('--seconds must be 2..30')
    if args.action is not None and not 0 <= args.action <= 65535:
        parser.error('--action must be 0..65535')
    link = Link(args.interface, args.action)
    try:
        if args.command == 'status':
            link.wait()
        elif args.command == 'stop':
            link.stop()
        elif args.command == 'mode':
            link.mode(['disabled', 'local', 'remote'].index(args.mode))
        else:
            stream(link, args)
    except (Exception, KeyboardInterrupt):
        # A best-effort GLOBAL_STOP needs no epoch/new action. TTL remains the
        # fallback if the bus is unavailable. Never claim STOP arrived here.
        try:
            link.send(0x182, struct.pack('<BBH4x', 1, 1, link.action or 0))
        except OSError:
            pass
        raise
    finally:
        link.sock.close()


if __name__ == '__main__':
    main()
