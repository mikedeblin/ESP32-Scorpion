#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mike Deblin
# Claude (01.10): .tap -> tape.h ("кассета в магнитофоне" для скетча).
#   Использование:  python3 tools/tap2h.py game.tap > tape.h
#   Загрузка: в меню Скорпиона "48 BASIC", затем F12 (перемотка + LOAD "") — или LOAD "" вручную.
#   Формат TAP: блоки [длина 2 байта LE][флаг][данные][контрольная сумма XOR].
import sys, os

if len(sys.argv) != 2:
    sys.exit("usage: tap2h.py <file.tap> > tape.h")
d = open(sys.argv[1], "rb").read()
pos, blocks = 0, 0
while pos + 2 <= len(d):                      # проверка структуры: блоки должны сойтись с длиной файла
    n = d[pos] | (d[pos + 1] << 8)
    pos += 2 + n
    blocks += 1
if pos != len(d) or blocks == 0:
    sys.exit(f"{sys.argv[1]}: not a valid .tap (blocks don't add up to file length)")
name = os.path.basename(sys.argv[1]).replace("\\", "/").replace('"', "'")
out = ["// Claude: сгенерировано tools/tap2h.py — не править руками",
       f"// {name}: {len(d)} байт, {blocks} блоков",
       "#pragma once", "#include <stdint.h>",
       f'#define TAPE_NAME "{name}"',
       f"static const uint8_t tape_image[{len(d)}] = {{"]
for i in range(0, len(d), 16):
    out.append("  " + ",".join(f"0x{b:02X}" for b in d[i:i + 16]) + ",")
out.append("};")
print("\n".join(out))
