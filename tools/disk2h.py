#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mike Deblin
# Claude: Образы дисков -> disks.h для скетча (вставляются в дисководы A и B при старте,
#   если нет ни SD-карты с образами, ни образов во flash).
#   Использование:  python3 tools/disk2h.py game1.scl [game2.trd] > disks.h
#   Поддерживаются .trd (до 640 КБ) и .scl. У .trd отрезаются нулевые хвостовые секторы —
#   во flash меньше места (эмулятор дополнит образ нулями до 640 КБ).
import sys, os

files = sys.argv[1:]
if not 1 <= len(files) <= 4:
    sys.exit("usage: disk2h.py img1 [img2 ...] > disks.h  (1..4 files .trd/.scl)")
out = ["// Claude: сгенерировано tools/disk2h.py — не править руками", "#pragma once", "#include <stdint.h>", ""]
for i, fn in enumerate(files):
    d = open(fn, "rb").read()
    if d[:8] == b"SINCLAIR":
        kind = "scl"
    else:
        kind = "trd"
        if len(d) > 655360:
            sys.exit(f"{fn}: .trd larger than 640 KB")
        n = len(d)
        while n >= 256 and not any(d[n - 256:n]):
            n -= 256
        d = d[:n]
    out.append(f"// {os.path.basename(fn)} ({kind}, {len(d)} bytes)")
    out.append(f"static const uint8_t disk{i}_data[{len(d)}] = {{")
    for j in range(0, len(d), 16):
        out.append("  " + ",".join(f"0x{b:02X}" for b in d[j:j + 16]) + ",")
    out.append("};")
    out.append("")
n = len(files)
out.append(f"#define DISK_COUNT {n}")
out.append("static const uint8_t *const disk_data[DISK_COUNT] = {" + ", ".join(f"disk{i}_data" for i in range(n)) + "};")
out.append("static const uint32_t disk_len[DISK_COUNT] = {" + ", ".join(f"sizeof(disk{i}_data)" for i in range(n)) + "};")
out.append("static const char *const disk_name[DISK_COUNT] = {" + ", ".join('"%s"' % os.path.basename(f) for f in files) + "};")
print("\n".join(out))
