#!/usr/bin/env python3
"""Mark a .nds as DSi-enhanced (unit code 0x02) and fix the header CRC, so launchers can start it in DSi mode (16 MB RAM)."""
import sys
p = sys.argv[1]
b = bytearray(open(p, 'rb').read())
b[0x12] = 0x02
crc = 0xFFFF
for byte in b[:0x15E]:
    crc ^= byte
    for _ in range(8):
        crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
b[0x15E:0x160] = crc.to_bytes(2, 'little')
open(p, 'wb').write(b)
print("patched", p, "unit code 0x02, header crc %04X" % crc)
