#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mike Deblin
# Claude (01.10): ROM Scorpion ZS-256 2.95 (64 КБ) -> rom_scorpion.h для скетча.
#   Использование:  python3 tools/rom2h.py scorpion295.rom > rom_scorpion.h
#   ROM в репозитории не лежит: в нём чужой код (Скорпион, Basic 48/128, TR-DOS 5.03).
#   Раскладка страниц: 0 = Basic 128 (меню), 1 = Basic 48, 2 = теневой монитор, 3 = TR-DOS.
#   Проверенный файл: CRC32 0C6C1EF6. Другая версия ROM не проверялась — скрипт предупредит.
import sys, os, zlib

if len(sys.argv) != 2:
    sys.exit("usage: rom2h.py <Scorpion ROM 2.95, 65536 bytes> > rom_scorpion.h")
d = open(sys.argv[1], "rb").read()
if len(d) != 65536:
    sys.exit(f"{sys.argv[1]}: {len(d)} bytes, need 65536 (4 pages x 16 KB)")
crc = zlib.crc32(d) & 0xFFFFFFFF
if crc != 0x0C6C1EF6:
    print(f"WARNING: CRC32 {crc:08X}, tested ROM is 0C6C1EF6 (Scorpion 2.95)", file=sys.stderr)
out = ["// Claude: сгенерировано tools/rom2h.py — не править руками",
       "// ROM Scorpion ZS-256 (64 КБ, 4 страницы по 16 КБ):",
       "//   0 = Basic 128 (меню Скорпиона), 1 = Basic 48, 2 = Теневой монитор, 3 = TR-DOS 5.03.",
       f"//   Из файла {os.path.basename(sys.argv[1])} (CRC32 {crc:08X}).",
       "#pragma once", "#include <stdint.h>",
       "static const uint8_t rom_scorpion[65536] = {"]
for i in range(0, len(d), 16):
    out.append("  " + ",".join(f"0x{b:02X}" for b in d[i:i + 16]) + ",")
out.append("};")
print("\n".join(out))
