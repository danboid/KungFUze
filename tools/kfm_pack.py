#!/usr/bin/env python3
"""Stamp the game data for packing into kungfum.uze (or for use as a separate KFMG.DAT).

    python3 tools/kfm_pack.py build/KFMG.DAT _obj_/KFMG.DAT

Writes into the data's header, which the asset tools leave zero there:
  bytes 16-17  CRC-16 (CCITT, polynomial 0x1021, initial value 0, MSB first: XMODEM), little endian,
               over the whole file with these two bytes taken as zero
  bytes 18-21  the file's length, little endian
The game computes the same CRC (avr-libc's _crc_xmodem_update) while loading the data into SPI RAM,
and stops on a white screen if the two differ.
"""
import binascii
import sys
from pathlib import Path

CRC_AT, LEN_AT = 16, 18


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    data = bytearray(src.read_bytes())
    if data[:4] != b'KFM7':
        raise SystemExit('%s: not the game data (no KFM7 marker)' % src)
    if any(data[CRC_AT:LEN_AT + 4]):
        raise SystemExit('%s: header bytes %d-%d are not free' % (src, CRC_AT, LEN_AT + 3))
    data[LEN_AT:LEN_AT + 4] = len(data).to_bytes(4, 'little')
    crc = binascii.crc_hqx(bytes(data), 0)        # (bytes 16-17 are zero here)
    data[CRC_AT:CRC_AT + 2] = crc.to_bytes(2, 'little')
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bytes(data))
    print('%s: %d bytes, CRC-16 0x%04X' % (dst, len(data), crc))


if __name__ == '__main__':
    main()
