#!/usr/bin/env python3
"""Exploratory survey of captured SPL/QPDL jobs (M1 decode groundwork).

Walks the record stream after '@PJL ENTER LANGUAGE=QPDL' using the layout observed in the
vendor fixtures and described on the SpliX specification page:

    0x00  begin page   17 bytes: type, yres/100, paper, ?, ?, width@300dpi(2), height@300dpi(2),
                        source, ?(2), ?(2), qpdl version, ?, xres/100
    0x0C  band data    11 bytes: type, band#, width(2), height(2), compression, length(4), then data
    0x01  end page      3 bytes: type, copies(2)
    0x09  end of job    1 byte, followed by the UEL <ESC>%-12345X

All multi-byte integers are big-endian. This is a survey tool, not the product decoder
(that is src/qpdl/decode.c). Output: one line per job plus anomalies.
"""
import glob
import os
import struct
import sys
from collections import Counter

UEL = b"\x1b%-12345X"


def survey(path):
    data = open(path, "rb").read()
    start = data.find(b"ENTER LANGUAGE=QPDL\r\n")
    if start < 0:
        return f"{os.path.basename(path)}: no QPDL entry"
    i = start + len(b"ENTER LANGUAGE=QPDL\r\n")
    pages, notes = [], []
    cur = None
    while i < len(data):
        t = data[i]
        if t == 0x00:
            cur = {"hdr": data[i:i + 17].hex(" "), "bands": [], "copies": None}
            pages.append(cur)
            i += 17
        elif t == 0x0C:
            band, width, height, comp, length = struct.unpack(">BHHBI", data[i + 1:i + 11])
            payload = data[i + 11:i + 11 + length]
            cur["bands"].append((band, width, height, comp, length, payload[:4].hex()))
            i += 11 + length
        elif t == 0x01:
            cur["copies"] = struct.unpack(">H", data[i + 1:i + 3])[0]
            i += 3
        elif t == 0x09:
            i += 1
            if data[i:i + len(UEL)] != UEL:
                notes.append(f"no UEL after end-of-job at {i}")
            i += len(UEL)
            if i != len(data):
                notes.append(f"{len(data) - i} trailing bytes after UEL")
            break
        else:
            notes.append(f"unknown record 0x{t:02x} at offset {i}")
            break
    out = [f"{os.path.basename(path)}: {len(pages)} page(s)"]
    for n, p in enumerate(pages):
        comps = Counter(b[3] for b in p["bands"])
        widths = sorted({b[1] for b in p["bands"]})
        heights = sorted({b[2] for b in p["bands"]})
        nums = [b[0] for b in p["bands"]]
        magic = Counter(b[5] for b in p["bands"])
        total = sum(b[4] for b in p["bands"])
        out.append(f"   page {n}: hdr={p['hdr']} copies={p['copies']}")
        out.append(f"      bands={len(nums)} numbers={nums[:3]}..{nums[-3:] if nums else []} "
                   f"widths={widths} heights={heights} compression={dict(comps)} "
                   f"payload_prefix={dict(magic)} data_bytes={total}")
    for n in notes:
        out.append(f"   ! {n}")
    return "\n".join(out)


if __name__ == "__main__":
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(os.path.dirname(__file__), "..", "fixtures", "oracle", "samsung", "*.spl")))
    for f in files:
        print(survey(f))
