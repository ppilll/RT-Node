"""Check Intel HEX checksums, addressing, and every data byte against reserved page."""
import sys
from pathlib import Path


def check(path):
    base, low, high, count, eof = 0, None, 0, 0, False
    for line in Path(path).read_text(encoding="ascii").splitlines():
        if not line:
            continue
        assert not eof and line.startswith(":"), "invalid record/order"
        data = bytes.fromhex(line[1:])
        assert len(data) == data[0] + 5 and sum(data) % 256 == 0, "checksum/length"
        size, offset, kind = data[0], int.from_bytes(data[1:3], "big"), data[3]
        payload = data[4:-1]
        if kind == 0:
            assert offset + size <= 0x10000, "address wrap"
            for address in range(base + offset, base + offset + size):
                assert 0x08000000 <= address < 0x0807F800, f"unsafe data: {address:#010x}"
                low = address if low is None else min(low, address)
                high = max(high, address)
                count += 1
        elif kind == 1:
            assert size == 0
            eof = True
        elif kind in (2, 4):
            assert size == 2 and offset == 0
            base = int.from_bytes(payload, "big") << (4 if kind == 2 else 16)
        else:
            assert kind in (3, 5) and size == 4, "unsupported record"
    assert eof and count > 0, "missing EOF/application data"
    return f"HEX PASS: {count} data bytes, {low:#010x}..{high:#010x}; reserved 0x0807f800..0x0807ffff absent"


if __name__ == "__main__":
    print(check(sys.argv[1]))
