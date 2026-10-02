// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mike Deblin
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See LICENSE.
//
// ESP32-Scorpion — Scorpion ZS-256 (ZX Spectrum-совместимый) на ESP32-S3, VGA 640x480@60
//
// Claude: Этап "Скорпион" (шаг 1: память и страницы). Машина теперь — Scorpion ZS-256
//   с ROM 2.95 (rom_scorpion.h): 256 КБ ОЗУ (16 страниц), 4 страницы ROM, порты
//   #7FFD/#1FFD, сигнал DOS/ (TR-DOS и теневой монитор). Модель железа восстановлена
//   по "Руководству по эксплуатации Scorpion ZS-256" и по коду самого ROM 2.95:
//     страница ROM = #1FFD.D1 ? 2 (Monitor) : (DOS ? 2 : 0) + #7FFD.D4
//     DOS/ включается: выборка из #3Dxx при Basic 48 (как Beta 128) или по NMI (Magic);
//     DOS/ выключается: выборка команды из ОЗУ (>= #4000).
//   Magic (NMI) — F11, срабатывает, когда процессор выполняет код из ОЗУ (как в оригинале).
//   F10 или Ctrl+Alt+Del — сброс машины (как кнопка "Сброс": ОЗУ не очищается).  TR-DOS — ВГ93 (секция FDC).
//
// Claude: Звук (шаг AY): AY-3-8912 (1.75 МГц) + бипер, синтез по событиям с точностью до
//   такта, 31250 Гц, вывод встроенным сигма-дельта модулятором ESP32-S3 на пины:
//   GPIO17 = левый (или моно), GPIO18 = правый. На каждый пин — RC-фильтр:
//     GPIO --[1к]--+--[1к]--+--[10мкФ +]--> Line In усилителя
//                 [10н]    [10н]
//                  GND      GND
//   AUDIO_MODE: 0 = моно (только GPIO17), 1 = стерео ABC, 2 = стерео ACB.
//
// Claude: Этапы 1+2. Что сделано по сравнению с версией Арча:
//   1) ОЗУ 48 КБ (было 32), вся память — один массив 64 КБ (см. z80user.h);
//   2) кадр 69888 тактов (было 6988 — процессор шёл в 10 раз медленнее);
//   3) порт ULA по-настоящему: чётный адрес, бордюр/бипер (выдуманные порты убраны);
//   4) прерывание — в начале кадра, темп 50 Гц держится по часам (esp_timer),
//      а не delay(20) поверх времени эмуляции;
//   5) VGA через аппаратный RGB-контроллер (как в VgaGrid v5), но БЕЗ кадрового
//      буфера: драйвер просит заполнить очередные 8 строк, и мы рисуем их прямо
//      из видеопамяти Спектрума — как это делает настоящая ULA.
//   6) Z80 крутится в loop() на ядре 1, видеопрерывание — на ядре 0.
//
// Claude (01.10): Цвета 15 (как у Спектрума): R/G/B + BRIGHT (R+/G+/B+ на GPIO14/15/16,
//   через буфер 74AC244 и резисторы 360 Ом / 1.5 кОм на один контакт VGA). FLASH работает.
//
// Claude: Этап 3 — USB-клавиатура (официальная библиотека ядра USBHostHIDKeyboard,
//   стек TinyUSB). Клавиатура/приёмник — в разъём "USB" (через OTG-переходник),
//   компьютер — в разъём "COM". Оба разъёма в компьютер одновременно НЕ включать.
//   На плате-клоне должна быть запаяна перемычка USB-OTG (питание 5 В на порт USB).
//
// Claude (01.10): Лента (необязательно): tape.h из своего .tap (tools/tap2h.py). Нет tape.h —
//   магнитофон пуст, LOAD "" ждёт, как без кассеты (Esc = BREAK). Загрузка — перехватом процедуры
//   ROM LD-BYTES (#0556), мгновенно. Набрать LOAD "" можно вручную (J, Ctrl+P, Ctrl+P,
//   Enter) или нажать F12: перемотка ленты + автонабор LOAD "" и Enter.
//   На Скорпионе: в меню выбрать "48 BASIC", затем F12 (набор рассчитан на режим K 48-го
//   Бейсика). Перехват стоит в КОПИИ страницы ROM 1 (Basic 48) в ОЗУ.
//
// Arduino IDE (ОБЯЗАТЕЛЬНО):
//   Board: ESP32S3 Dev Module
//   USB Mode: USB-OTG (TinyUSB)
//   USB CDC On Boot: Disabled      <- иначе Serial уйдёт в порт, занятый клавиатурой
//   PSRAM: OPI PSRAM               <- ОБЯЗАТЕЛЬНО: 12 из 16 страниц ОЗУ Скорпиона лежат в PSRAM
//   Flash Size: 16MB, Partition Scheme: 16M Flash (2MB APP/12.5MB FATFS)
//   Upload / Serial Monitor — через разъём COM (UART).
// Claude (01.10): Проводка (подробно — README):
//   VGA через 74AC244: R/G/B = GPIO4/5/6, R+/G+/B+ (BRIGHT) = GPIO14/15/16, HSYNC/VSYNC = GPIO7/8.
//   Звук I2S PCM5102: BCK = GPIO1, LCK = GPIO2, DIN = GPIO9.  SD (SPI): CS 10, MOSI 11, CLK 12, MISO 13.

#include "z80emu.h"
// Claude (01.10.2026): с v19 run_frame() держит HALT сам — нужен z80config.h с Z80_CATCH_HALT.
//   Старый z80config.h (до v19) молча ломал бы программы: HALT без ожидания прерывания.
#ifndef Z80_CATCH_HALT
#error "z80config.h is outdated: take z80config.h from the same release as ESP32-Scorpion.ino"
#endif
// Claude (01.10): ЧТО: ROM и лента — не в репозитории, их делает сам пользователь.
//   ПОЧЕМУ: в ROM 2.95 — чужой код (Скорпион, Basic 48/128, TR-DOS), на игры — права авторов.
//   rom_scorpion.h: python3 tools/rom2h.py <ROM Scorpion 2.95, 64 КБ> > ESP32-Scorpion/rom_scorpion.h
//   tape.h (необязательно): python3 tools/tap2h.py <игра.tap> > ESP32-Scorpion/tape.h
#if !__has_include("rom_scorpion.h")
#error "Нет rom_scorpion.h: python3 tools/rom2h.py scorpion295.rom > ESP32-Scorpion/rom_scorpion.h (см. README)"
#endif
#include "rom_scorpion.h"            // Claude: ROM Scorpion ZS-256 v2.95, 4 страницы по 16 КБ
#if __has_include("tape.h")
#include "tape.h"                    // Claude (01.10): лента в "магнитофоне" (tape_image, TAPE_NAME)
#endif
// Claude: Образы дисков для TR-DOS (необязательно). disks.h делает tools/disk2h.py из
//   .trd/.scl; вставляются в A и B, если нет образов на SD и во flash.
// Claude: USB-хост (клавиатура) и USB-диск работают только через TinyUSB
#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE != 0
#error "Tools -> USB Mode -> USB-OTG (TinyUSB)"
#endif
#if __has_include("disks.h")
#include "disks.h"
#endif
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include <esp_timer.h>
#include "esp_cpu.h"                 // Claude: esp_cpu_get_cycle_count() для отладки видео
#include "esp_heap_caps.h"            // Claude: выделение памяти в PSRAM
#include <USBHost.h>                  // Claude: USB-хост (TinyUSB), входит в ядро 3.3.x
#include <USBHostHIDKeyboard.h>       // Claude: драйвер boot-клавиатуры, входит в ядро 3.3.x
#include "esp_vfs_fat.h"               // Claude: FAT во flash только на чтение (без wear levelling)
#include <dirent.h>
#include "USB.h"                      // Claude: USB-устройство (режим диска)
#include "USBMSC.h"
#include "esp_partition.h"
#include "esp_system.h"
#include <SPI.h>                      // Claude (01.10): SD-карта по SPI
#include <SD.h>

// Claude: Защита от неправильной настройки: с "USB CDC On Boot: Enabled" Serial
//   живёт на нативном USB, который теперь занят клавиатурой — лог бы пропал.
#if ARDUINO_USB_CDC_ON_BOOT
#error "Tools -> USB CDC On Boot -> Disabled (native USB is used by the keyboard)"
#endif
// Claude: 256 КБ ОЗУ Скорпиона во внутреннюю RAM не помещаются — нужна PSRAM.
#ifndef BOARD_HAS_PSRAM
#error "Tools -> PSRAM -> OPI PSRAM (Scorpion RAM pages live in PSRAM)"
#endif

static volatile uint32_t zx_frame = 0;      // Claude: счётчик кадров эмуляции (для FLASH)
static Z80_STATE z80;
// Claude (01.10.2026): замок для обмена буферами кадра между Z80 (ядро 1) и VGA (прерывание,
//   ядро 0). Держится на пару команд. Тест на ПК подставляет пустые FB_LOCK/FB_UNLOCK.
static portMUX_TYPE fb_mux = portMUX_INITIALIZER_UNLOCKED;
#define FB_LOCK()   portENTER_CRITICAL(&fb_mux)
#define FB_UNLOCK() portEXIT_CRITICAL(&fb_mux)

// ==== MACHINE BEGIN (Claude: память и страницы Скорпиона; прогоняется тестом на ПК) ====
// ---------------------------------------------------------------------------
// Claude: Память. 4 окна по 16 КБ (см. z80user.h). extern "C" — их читает z80emu.c.
// ---------------------------------------------------------------------------
extern "C" {
  unsigned char *zx_rd[4];                  // откуда читать окно 0..3
  unsigned char *zx_wr[4];                  // куда писать (NULL = ROM)
  unsigned char  zx_dos = 0;                // 1 = DOS/ активен (ROM Monitor / TR-DOS)
  unsigned char  zx_rom_page = 0;           // страница ROM в окне 0
  unsigned char  zx_rom0_is_rom = 1;        // 0 = в окне 0 ОЗУ 0 (#1FFD.D0)
  volatile unsigned char zx_border = 7;     // цвет бордюра 0..7
  volatile unsigned char zx_beeper = 0;
  int            zx_tbase = 0;              // такт кадра на момент начала текущего Z80Emulate
}
// Claude: Страницы 0, 2, 5, 7 — во внутренней RAM: 5 и 7 — это экраны, их читает
//   видеопрерывание; 0 и 2 — самые "ходовые" (2 всегда в #8000). Остальные 12 — PSRAM.
static uint8_t  ram_int[4][0x4000];
static uint8_t  rom1_ram[0x4000];           // копия ROM Basic 48 (в ней перехват ленты #0556)
static uint8_t *zx_ram[16];
static const uint8_t *zx_rom[4];
static uint8_t  p7ffd = 0, p1ffd = 0;       // порты Скорпиона
static uint8_t * volatile zx_screen = nullptr;  // экран для видео: ОЗУ 5 или 7

// Claude (01.10.2026): БУФЕРЫ КАДРА "ПО ЛУЧУ".
//   ЧТО: run_frame() по ходу кадра копирует каждую строку экрана (32 байта пикселей +
//   32 байта атрибутов) и цвет бордюра в тот такт, когда её рисовал бы луч ТВ.
//   VGA рисует из готового буфера, а не из живой памяти.
//   ПОЧЕМУ: ESP прогоняет кадр Z80 залпом за несколько мс, а VGA читает память в своём
//   темпе. Демки, которые рисуют за лучом и тут же стирают (The Lyra II, скроллер в
//   начале: нарисовать -> ждать ~5200 тактов -> стереть, каждый кадр), на ТВ видны
//   целиком, а у нас VGA ловила то пусто, то полкартинки ("темно - кадр - темно").
//   Проверено на ПК на этой демке: по лучу текст виден в каждом кадре.
//   Атрибуты по строкам (а не по знакоместу) — заодно работает мультиколор.
//   Буферов три: один показывает VGA, один готов к показу, в третий пишет Z80 —
//   никто никому не мешает, разрывов кадра нет.
//   Бордюр: 240 строк Спектрума (24 над бумагой, 192, 24 под) — сколько видно на VGA.
//   ОТКЛОНИЛИ: копию экрана в конце кадра — у такой демки в конце кадра экран пуст.
static const int FB_LINES = 240, FB_TOP = 24;          // строк бордюра+бумаги; строк над бумагой
static const int FB_T_PAPER = 14336;                   // такт первой строки бумаги (Fuse/libspectrum, Scorpion)
static const int FB_T_LINE = 224;                      // тактов в строке
static uint8_t fb_pix[3][6144];                        // пиксели, раскладка как в ОЗУ Спектрума
static uint8_t fb_att[3][192 * 32];                    // атрибуты отдельно для каждой строки
static uint8_t fb_bord[3][FB_LINES];                   // цвет бордюра для каждой строки
static volatile int8_t fb_front = 0, fb_ready = -1;    // показывается / готов к показу (-1 = нет)
static int8_t fb_back = 1;                             // сюда пишет текущий кадр Z80
static volatile bool fb_on = false;                    // false = VGA рисует живой экран (меню, старт)
static uint8_t  ay_reg[16], ay_sel = 0;     // AY: регистры "со стороны процессора" (для чтения)

// Claude: Раскладка: psram_pages — блок 12*16 КБ (в PSRAM) для страниц 1,3,4,6,8..15.
static void zx_mem_init(uint8_t *psram_pages) {
  int j = 0;
  for (int pg = 0; pg < 16; pg++) {
    if      (pg == 0) zx_ram[pg] = ram_int[0];
    else if (pg == 2) zx_ram[pg] = ram_int[1];
    else if (pg == 5) zx_ram[pg] = ram_int[2];
    else if (pg == 7) zx_ram[pg] = ram_int[3];
    else              zx_ram[pg] = psram_pages + (j++) * 0x4000;
    memset(zx_ram[pg], 0, 0x4000);
  }
  memcpy(rom1_ram, rom_scorpion + 0x4000, 0x4000);
  zx_rom[0] = rom_scorpion;                 // Basic 128 (меню)
  zx_rom[1] = rom1_ram;                     // Basic 48 (копия с перехватом ленты)
  zx_rom[2] = rom_scorpion + 0x8000;        // Теневой монитор
  zx_rom[3] = rom_scorpion + 0xC000;        // TR-DOS 5.03
}

// Claude: Пересчитать окна по портам и DOS/. Вызывается при записи в #7FFD/#1FFD и смене DOS/.
static void zx_remap() {
  uint8_t rp = (p1ffd & 0x02) ? 2 : (uint8_t)((zx_dos ? 2 : 0) + ((p7ffd >> 4) & 1));
  zx_rom_page = rp;
  if (p1ffd & 0x01) {                       // #1FFD.D0: ОЗУ 0 вместо ROM (чтение и запись)
    zx_rom0_is_rom = 0;
    zx_rd[0] = zx_wr[0] = zx_ram[0];
  } else {
    zx_rom0_is_rom = 1;
    zx_rd[0] = (unsigned char *)zx_rom[rp];
    zx_wr[0] = nullptr;
  }
  zx_rd[1] = zx_wr[1] = zx_ram[5];
  zx_rd[2] = zx_wr[2] = zx_ram[2];
  uint8_t cp = (p7ffd & 0x07) | ((p1ffd & 0x10) ? 8 : 0);   // #1FFD.D4: страницы 8..15
  zx_rd[3] = zx_wr[3] = zx_ram[cp];
  zx_screen = zx_ram[(p7ffd & 0x08) ? 7 : 5];
}

extern "C" void zx_dos_set(int on) { zx_dos = on ? 1 : 0; zx_remap(); }

static void zx_machine_reset() {            // Claude: как кнопка "Сброс": порты в 0, DOS выкл.
  p7ffd = 0; p1ffd = 0; zx_dos = 0; ay_sel = 0;
  memset(ay_reg, 0, sizeof(ay_reg));
  zx_remap();
  Z80Reset(&z80);
}

static inline uint8_t zx_peek(uint16_t a) { return zx_rd[a >> 14][a & 0x3FFF]; }
static inline void zx_poke(uint16_t a, uint8_t v) { uint8_t *w = zx_wr[a >> 14]; if (w) w[a & 0x3FFF] = v; }
// ==== MACHINE END ====

static const int FRAME_TSTATES = 69888;     // Claude: 3.5 МГц / 50 Гц (было 6988)
static const int FRAME_US      = 20000;     // Claude: 1/50 с в микросекундах
#define AUDIO_MODE    2                        // Claude: 0 = моно, 1 = стерео ABC, 2 = стерео ACB
#define AUDIO_PIN_L   17                       // Claude: левый канал (или моно)
#define AUDIO_PIN_R   18                       // Claude: правый канал (не используется в моно)
// Claude (01.10): ЧТО: куда выводить звук. 1 = I2S на PCM5102 (основной), 0 = старый ШИМ
//   (сигма-дельта на GPIO17/18 через RC-фильтры) — оставлен запасным.
//   ПОЧЕМУ 48 кГц: стандартная частота для PCM5102 и ровно 960 сэмплов на кадр 50 Гц.
#define AUDIO_OUT     1
#define I2S_PIN_BCK   1                        // Claude (01.10): PCM5102 BCK
#define I2S_PIN_WS    2                        //                 PCM5102 LCK (LRCK)
#define I2S_PIN_DOUT  9                        //                 PCM5102 DIN; SCK модуля — на GND
#if AUDIO_OUT == 1
static const int AUDIO_RATE = 48000;           // Claude (01.10): 960 сэмплов на кадр — ровно
#else
static const int AUDIO_RATE = 31250;           // Claude: частота сэмплов, Гц (625 на кадр — ровно)
#endif
static const int AUDIO_SPF  = AUDIO_RATE / 50; // сэмплов на кадр

// ---------------------------------------------------------------------------
// VGA 640x480@60 — те же тайминги и пины, что в рабочем VgaGrid v5
// ---------------------------------------------------------------------------
const int PIN_R = 4, PIN_G = 5, PIN_B = 6;
// Claude (01.10): ЧТО: вторые линии каналов для BRIGHT (R+, G+, B+).
//   ПОЧЕМУ линии данных панели, а не обычные GPIO: их выставляет тот же DMA, что и R/G/B,
//   точно в такт пикселя — никакой отдельной программной работы.
//   НА ЧТО НЕ НАСТУПАТЬ: эти пины обязательно должны быть выходами. Пока прошивка их не
//   использовала, входы буфера висели в воздухе и генерировали — срыв кадровой синхры,
//   скачущие полосы, лечилось касанием земли (01.10).
const int PIN_RB = 14, PIN_GB = 15, PIN_BB = 16;
const int PIN_HSYNC = 7, PIN_VSYNC = 8;
static const int VGA_W = 640, VGA_H = 480;

// Claude (01.10.2026): ЧАСТОТА КАДРОВ VGA — 50 (как у Спектрума) или 60 (стандарт VGA).
//   ПОЧЕМУ 50: эмуляция идёт 50 кадров/с. При 60 Гц монитор каждый 5-й кадр показывает
//   дважды — плавный скролл в демках "спотыкается". При 50 Гц кадр на кадр.
//   50 Гц — нестандартный для VGA режим: если монитор не покажет — поставить 60.
// Claude (01.10.2026): по умолчанию 60 (решение Майка после проверки v19 на LG): с экраном
//   "по лучу" разрывов нет и на 60 Гц, остаётся только повтор каждого 5-го кадра; а 50 Гц
//   на LG сжимает картинку по вертикали. 50 — для тех, у кого монитор его тянет.
#ifndef VGA_HZ
#define VGA_HZ 60
#endif
#if VGA_HZ != 50 && VGA_HZ != 60
#error "VGA_HZ: только 50 или 60"
#endif

// Claude: Экран Спектрума 256x192, удвоенный = 512x384, по центру.
//   Слева/справа бордюр по 64 пикселя (16 слов по 4 байта), сверху/снизу по 48 строк.
static const int SCR_TOP = 48, SCR_LINES = 384;
static const int BORDER_WORDS = 16;          // 64 px / 4

// Claude: Таблицы для быстрой отрисовки. Обычные (не const) глобальные массивы
//   лежат во внутренней RAM — прерывание не лезет во flash.
static uint32_t zx_color_w[16];  // Claude (01.10): цвет 0..7 + BRIGHT (8..15) -> 4 одинаковых байта VGA-пикселя
static uint32_t mask_hi[16];     // 4 бита Спектрума -> 8 VGA-пикселей: первое слово
static uint32_t mask_lo[16];     //                                    второе слово

static void build_tables() {
  // Claude: Номер цвета Спектрума: бит0=синий, бит1=красный, бит2=зелёный.
  //   Наш байт пикселя: бит0=R (GPIO4), бит1=G (GPIO5), бит2=B (GPIO6),
  //   бит3=R+ (GPIO14), бит4=G+ (GPIO15), бит5=B+ (GPIO16).
  // Claude (01.10): ЧТО: индексы 8..15 — те же цвета с BRIGHT.
  //   ПОЧЕМУ так: яркий цвет = включить у горящих каналов ещё и линию "+" (v << 3).
  //   Яркий чёрный сам остаётся чёрным — у него нет горящих каналов, как на Спектруме.
  for (int i = 0; i < 16; i++) {
    uint32_t c = i & 7;
    uint32_t v = ((c >> 1) & 1) | (((c >> 2) & 1) << 1) | ((c & 1) << 2);
    if (i & 8) v |= v << 3;
    zx_color_w[i] = v * 0x01010101u;
  }
  // Claude: Каждый бит Спектрума = 2 VGA-пикселя (удвоение по горизонтали).
  //   Левый пиксель — старший бит, он же младший адрес (little-endian).
  for (int n = 0; n < 16; n++) {
    mask_hi[n] = ((n & 8) ? 0x0000FFFFu : 0) | ((n & 4) ? 0xFFFF0000u : 0);
    mask_lo[n] = ((n & 2) ? 0x0000FFFFu : 0) | ((n & 1) ? 0xFFFF0000u : 0);
  }
}

// ==== RENDER BEGIN (Claude: этот кусок также прогнан тестом на ПК) ====
// Claude: Рисует одну VGA-строку y (0..479) в dst (640 байт).
// Claude (01.10.2026): pix_base — 6144 байта пикселей в раскладке Спектрума; att — 32 атрибута
//   ИМЕННО ЭТОЙ строки (буфер "по лучу" хранит атрибуты построчно; для живого экрана
//   вызывающий передаёт строку атрибутов знакоместа). Для строк бордюра pix/att не нужны.
static inline void IRAM_ATTR render_line(int y, uint8_t *dst, uint32_t border_w, bool flash_inv,
                                         const uint8_t *pix_base, const uint8_t *att) {
  uint32_t *w = (uint32_t *)dst;
  if (y < SCR_TOP || y >= SCR_TOP + SCR_LINES) {
    for (int i = 0; i < VGA_W / 4; i++) w[i] = border_w;       // строка целиком — бордюр
  } else {
    int L = (y - SCR_TOP) >> 1;                                 // строка Спектрума 0..191
    // Claude: Хитрая адресация экрана Спектрума: треть / строка знакоместа / строка в знакоместе.
    const uint8_t *pix = &pix_base[((L & 0xC0) << 5) | ((L & 0x07) << 8) | ((L & 0x38) << 2)];
    for (int i = 0; i < BORDER_WORDS; i++) *w++ = border_w;    // левый бордюр
    for (int c = 0; c < 32; c++) {
      uint8_t a = att[c];
      // Claude (01.10): BRIGHT (бит 6 атрибута) -> +8 к номеру цвета, и для чернил, и для бумаги.
      uint32_t br    = (a >> 3) & 8;
      uint32_t ink   = zx_color_w[(a & 7) | br];
      uint32_t paper = zx_color_w[((a >> 3) & 7) | br];
      if ((a & 0x80) && flash_inv) { uint32_t t = ink; ink = paper; paper = t; }   // FLASH
      uint8_t p = pix[c];
      uint32_t m;
      m = mask_hi[p >> 4];  w[0] = (ink & m) | (paper & ~m);
      m = mask_lo[p >> 4];  w[1] = (ink & m) | (paper & ~m);
      m = mask_hi[p & 15];  w[2] = (ink & m) | (paper & ~m);
      m = mask_lo[p & 15];  w[3] = (ink & m) | (paper & ~m);
      w += 4;
    }
    for (int i = 0; i < BORDER_WORDS; i++) *w++ = border_w;    // правый бордюр
  }
  // Claude: Страховка из VgaGrid: последний пиксель строки чёрный (уровень чёрного
  //   в служебной зоне строки). На глаз не видно.
  dst[VGA_W - 1] = 0;
}
// ==== RENDER END ====

// Claude: Драйвер вызывает это в прерывании (ядро 0), когда bounce-буфер
//   освободился: заполнить len_bytes байт, начиная с пикселя pos_px кадра.
// Claude: отладка — промежутки между вызовами колбэка (такты CPU 240 МГц).
//   Норма: BB_LINES строк * 763 точки / 24 МГц (8 строк = 254 мкс). Сильно больше — колбэк
//   запаздывает, и начало буфера успевает уйти на экран старым (черточки у края бумаги).
static volatile uint32_t bb_last_cc = 0, bb_gap_max = 0, bb_gap_min = 0xFFFFFFFF;
// Claude: строк в bounce-буфере. 480 должно делиться на BB_LINES, а SCR_TOP (48) — на него же,
//   чтобы кусок начинался с чётной строки.
static const int BB_LINES = 8;
// Claude: ОБХОД ОШИБКИ ДРАЙВЕРА (ESP-IDF 5.5, esp_lcd_panel_rgb.c). Буферов два, DMA после
//   перезапуска в каждом VSYNC всегда начинает с буфера 0, значит кусок кадра номер k всегда
//   выводится из буфера k%2. Драйвер же выбирает буфер для заполнения по счётчику EOF
//   (bb_eof_count % 2), а при CONFIG_LCD_RGB_RESTART_IN_VSYNC этот счётчик не обнуляется.
//   Если хоть одно EOF потерялось (прерывание LCD ждало, пока шло чтение/запись flash —
//   его обработчик не в IRAM), чётность навсегда сбивается: колбэку дают тот буфер, который
//   DMA как раз выводит, и начало куска уходит на экран старым (черточки у края бумаги).
//   Поэтому буфер выбираем сами по номеру куска; адреса буферов запоминаем при старте —
//   драйвер заполняет их по порядку: кусок 0 -> буфер 0, кусок 1 -> буфер 1.
static uint8_t *bb_ptr[2] = {nullptr, nullptr};
static volatile uint32_t bb_fixes = 0;                 // сколько раз драйвер дал "не тот" буфер
static bool IRAM_ATTR on_bounce_empty(esp_lcd_panel_handle_t, void *buf, int pos_px, int len_bytes, void *) {
  uint32_t cc = esp_cpu_get_cycle_count();
  if (bb_last_cc && pos_px != 0) {                  // через кадровый гасящий не меряем
    uint32_t g = cc - bb_last_cc;
    if (g > bb_gap_max) bb_gap_max = g;
    if (g < bb_gap_min) bb_gap_min = g;
  }
  bb_last_cc = cc;
  int k = pos_px / (VGA_W * BB_LINES);                 // номер куска в кадре
  if (k < 2 && !bb_ptr[k]) bb_ptr[k] = (uint8_t *)buf; // первые два вызова — старт передачи
  uint8_t *dst = bb_ptr[k & 1] ? bb_ptr[k & 1] : (uint8_t *)buf;
  if (dst != (uint8_t *)buf) bb_fixes++;
  int y = pos_px / VGA_W;
  int lines = len_bytes / VGA_W;
  bool flash_inv = (zx_frame >> 4) & 1;        // Claude: FLASH меняет фазу каждые 16 кадров, как в оригинале
  // Claude (01.10.2026): ЧТО: в начале кадра VGA (кусок 0) решаем, откуда рисовать весь кадр:
  //   из готового буфера "по лучу" (если Z80 выложил новый — берём его) или из живого экрана.
  //   ПОЧЕМУ в начале кадра: смена посреди кадра VGA дала бы разрыв — верх от одного
  //   кадра Спектрума, низ от другого.
  static int8_t cur_fb = -1;                   // -1 = живой экран
  if (k == 0) {
    if (fb_on) {
      portENTER_CRITICAL_ISR(&fb_mux);
      if (fb_ready >= 0) { fb_front = fb_ready; fb_ready = -1; }
      portEXIT_CRITICAL_ISR(&fb_mux);
      cur_fb = fb_front;
    } else {
      cur_fb = -1;
    }
  }
  if (cur_fb >= 0) {
    const uint8_t *pix = fb_pix[cur_fb];
    for (int i = 0; i < lines; i++) {
      int ln = (y + i) >> 1;                   // строка Спектрума с учётом бордюра: 0..239
      int L = ln - FB_TOP;                     // строка бумаги (0..191, иначе бордюр)
      const uint8_t *att = ((unsigned)L < 192) ? &fb_att[cur_fb][L * 32] : nullptr;
      render_line(y + i, dst + i * VGA_W, zx_color_w[fb_bord[cur_fb][ln] & 7], flash_inv, pix, att);
    }
  } else {
    uint32_t border_w = zx_color_w[zx_border & 7];
    const uint8_t *scr = zx_screen;            // Claude: экран, видимый в этот момент (5 или 7)
    for (int i = 0; i < lines; i++) {
      int L = ((y + i) - SCR_TOP) >> 1;
      const uint8_t *att = ((unsigned)L < 192) ? &scr[0x1800 + ((L >> 3) << 5)] : nullptr;
      render_line(y + i, dst + i * VGA_W, border_w, flash_inv, scr, att);
    }
  }
  return false;
}

// Claude (01.10.2026): ЧТО: кадровый импульс VGA будит loop() — при VGA_HZ 50 эмуляция
//   идёт в такт с монитором (каждый кадр Спектрума показывается ровно один раз).
//   ПОЧЕМУ не esp_timer: частоты равны (один кварц), но фаза таймера случайна и дрожит —
//   если выкладка кадра и смена буфера VGA окажутся рядом, пойдут пропуски/повторы.
static TaskHandle_t loop_task = nullptr;
static volatile uint32_t vsync_count = 0;
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  vsync_count = vsync_count + 1;
  BaseType_t woken = pdFALSE;
  if (loop_task) vTaskNotifyGiveFromISR(loop_task, &woken);
  return woken == pdTRUE;
}

static esp_lcd_panel_handle_t panel = nullptr;
static volatile int video_status = 0;          // 0 = ещё не готово, 1 = ок, <0 = ошибка
static volatile esp_err_t video_err = ESP_OK;

// Claude: Панель создаётся в задаче на ядре 0: драйвер вешает свои прерывания
//   на то ядро, где его создали. Так видео не мешает Z80 (он на ядре 1).
static void video_init_task(void *) {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_PLL240M;
  // Claude: Было 25.175 МГц — из 240 МГц точно не делится, драйвер ставил ДРОБНЫЙ
  //   делитель (чередует соседние), длительность пикселя "гуляла" на ~10%, и ЖК-монитор
  //   давал рябь "как через антенну" на мелких деталях шрифта.
  //   Теперь 24 МГц = 240 / 2 / 5 — делитель целый, все пиксели одинаковые.
  cfg.timings.pclk_hz = 24000000;
  cfg.timings.h_res = VGA_W;
  cfg.timings.v_res = VGA_H;
  // Claude: Чтобы частоты остались стандартными для 640x480@60 (строки ~31.5 кГц),
  //   строка укорочена с 800 до 763 тактов: 640 видимых + 123 служебных
  //   (было 16+96+48 = 160, пропорционально ужато до 12+74+37).
  //   HSYNC = 24e6/763 = 31.46 кГц, VSYNC = 31460/525 = 59.9 Гц.
  //   Монитор ждёт 800 тактов на строку — возможно понадобится автонастройка.
#if VGA_HZ == 50
  // Claude (01.10.2026): 50 Гц. Кадр = 24e6/50 = 480000 тактов = 768 x 625 — ровно,
  //   без остатка: строчная 31.25 кГц (удвоенный PAL), 625 строк, кадровая 50.000 Гц.
  //   ПОЧЕМУ так: pclk не трогаем (только целый делитель, см. выше); кадр ровно 50.000 Гц
  //   и эмуляция (esp_timer, тот же кварц) ровно 50.000 Гц — не расходятся, без
  //   периодического повтора/пропуска кадра.
  //   Строка: 640 + 128 (16/96/48 стандарта, ужато до 13/77/38).
  //   Кадр: 480 + 145 служебных; лишние строки поровну не делим — монитор сам
  //   центрирует (автонастройка). 763x629 отклонили: 50.007 Гц, повтор кадра раз в ~150 с.
  cfg.timings.hsync_pulse_width = 77;
  cfg.timings.hsync_back_porch  = 38;
  cfg.timings.hsync_front_porch = 13;
  cfg.timings.vsync_pulse_width = 2;
  cfg.timings.vsync_back_porch  = 99;
  cfg.timings.vsync_front_porch = 44;
#else
  cfg.timings.hsync_pulse_width = 74;
  cfg.timings.hsync_back_porch  = 37;
  cfg.timings.hsync_front_porch = 12;
  cfg.timings.vsync_pulse_width = 2;
  cfg.timings.vsync_back_porch  = 33;
  cfg.timings.vsync_front_porch = 10;
#endif
  cfg.timings.flags.hsync_idle_low = 0;        // отрицательные импульсы синхро (стандарт VGA)
  cfg.timings.flags.vsync_idle_low = 0;

  cfg.data_width = 8;
  cfg.bits_per_pixel = 8;
  cfg.num_fbs = 0;                              // Claude: кадрового буфера нет
  cfg.flags.no_fb = 1;                          // Claude: строки рисуем сами в on_bounce_empty
  // Claude: 8 строк на буфер: 640*480 / (640*8) = 60 — делится ровно (драйвер это требует).
  //   Кусок из 8 строк всегда начинается с чётной строки — удобно для удвоения.
  cfg.bounce_buffer_size_px = VGA_W * BB_LINES;
  cfg.dma_burst_size = 64;

  cfg.hsync_gpio_num = PIN_HSYNC;
  cfg.vsync_gpio_num = PIN_VSYNC;
  cfg.de_gpio_num = -1;
  cfg.pclk_gpio_num = -1;
  cfg.disp_gpio_num = -1;
  for (int i = 0; i < SOC_LCDCAM_RGB_DATA_WIDTH; i++) cfg.data_gpio_nums[i] = -1;
  cfg.data_gpio_nums[0] = PIN_R;
  cfg.data_gpio_nums[1] = PIN_G;
  cfg.data_gpio_nums[2] = PIN_B;
  cfg.data_gpio_nums[3] = PIN_RB;               // Claude (01.10): BRIGHT, см. PIN_RB
  cfg.data_gpio_nums[4] = PIN_GB;
  cfg.data_gpio_nums[5] = PIN_BB;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
  if (err == ESP_OK) {
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_bounce_empty = on_bounce_empty;     // Claude: регистрируем ДО init — init сразу заполняет буферы
    cbs.on_vsync = on_vsync;                   // Claude (01.10.2026): темп эмуляции от VGA (VGA_HZ 50)
    err = esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);
  }
  if (err == ESP_OK) err = esp_lcd_panel_reset(panel);
  if (err == ESP_OK) err = esp_lcd_panel_init(panel);

  video_err = err;
  video_status = (err == ESP_OK) ? 1 : -1;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Claude: КЛАВИАТУРА
//   Матрица Спектрума: 8 рядов по 5 клавиш. Ряд выбирается нулевым битом в
//   СТАРШЕМ байте адреса порта (A8..A15), нажатая клавиша = 0 в битах 0..4.
//     ряд 0 (#FEFE): CAPS Z X C V      ряд 4 (#EFFE): 0 9 8 7 6
//     ряд 1 (#FDFE): A S D F G         ряд 5 (#DFFE): P O I U Y
//     ряд 2 (#FBFE): Q W E R T         ряд 6 (#BFFE): ENTER L K J H
//     ряд 3 (#F7FE): 1 2 3 4 5         ряд 7 (#7FFE): SPACE SYM M N B
// ---------------------------------------------------------------------------
static volatile uint8_t zx_keys[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
// Claude: "Виртуальные" нажатия автонабора (F12 -> LOAD ""), складываются с реальными.
static volatile uint8_t inj_keys[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
static volatile bool    f12_request = false;   // Claude: F12 нажата -> в loop() запустить автозагрузку
static volatile bool    f11_request = false;   // Claude: F11 = кнопка Magic (NMI)
static volatile bool    f10_request = false;   // Claude: F10 = кнопка Сброс
// Claude (01.10.2026): Ctrl+Alt+End — полный перезапуск ESP32 (как кнопка на плате), по просьбе Майка:
//   F10 / Ctrl+Alt+Del сбрасывают только Скорпион; если заглючило глубже, тянуться к плате неудобно.
static volatile bool    hard_request = false;
static volatile bool    f9_request = false;    // Claude: F9 = меню дисков
static volatile bool    menu_active = false;   // Claude: открыто меню — клавиши идут в меню, не в Спектрум
static volatile uint8_t menu_key = 0;          // Claude: последняя НОВАЯ нажатая клавиша (код HID), 0 = нет
static volatile uint8_t menu_hold = 0;         // Claude (01.10): удерживаемая стрелка/PgUp/PgDn — для автоповтора

// Claude: Чтение матрицы: ряды выбираются нулями в старшем байте адреса порта.
static uint8_t kbd_read(uint8_t hi) {
  uint8_t r = 0x1F;
  for (int row = 0; row < 8; row++)
    if (!(hi & (1 << row))) r &= zx_keys[row] & inj_keys[row];   // можно выбрать несколько рядов сразу
  return r;
}

// Claude: Раскладка USB -> Спектрум. Код HID (0..255) -> клавиша Спектрума.
//   Младший байт: (ряд << 3) | бит, 0xFF = нет клавиши. Флаги: +CAPS / +SYMBOL SHIFT.
static const uint16_t KM_NONE = 0x00FF, KM_CAPS = 0x0100, KM_SYM = 0x0200;
static uint16_t kbd_map[256];
static const uint8_t ZX_CAPS = (0 << 3) | 0, ZX_SYM = (7 << 3) | 1;

static uint8_t zx_code_of(char ch) {
  // Claude: Раскладка рядов (как в таблице выше). '^' = CAPS, '$' = SYM, '\n' = ENTER.
  static const char *rows[8] = {"^ZXCV", "ASDFG", "QWERT", "12345", "09876", "POIUY", "\nLKJH", " $MNB"};
  for (int r = 0; r < 8; r++)
    for (int b = 0; b < 5; b++)
      if (rows[r][b] == ch) return (uint8_t)((r << 3) | b);
  return 0xFF;
}

static void build_kbd_map() {
  for (int i = 0; i < 256; i++) kbd_map[i] = KM_NONE;
  for (int i = 0; i < 26; i++) kbd_map[0x04 + i] = zx_code_of('A' + i);      // a..z
  for (int i = 0; i < 9; i++)  kbd_map[0x1E + i] = zx_code_of('1' + i);      // 1..9
  kbd_map[0x27] = zx_code_of('0');
  kbd_map[0x28] = zx_code_of('\n');                                          // Enter
  kbd_map[0x58] = zx_code_of('\n');                                          // Enter на цифровом блоке
  kbd_map[0x2C] = zx_code_of(' ');                                           // Пробел
  for (int i = 0; i < 9; i++)  kbd_map[0x59 + i] = zx_code_of('1' + i);      // цифровой блок 1..9
  kbd_map[0x62] = zx_code_of('0');                                           // цифровой блок 0
  // Claude: Удобные сочетания, как на "плюсовых" Спектрумах:
  kbd_map[0x2A] = KM_CAPS | zx_code_of('0');   // Backspace -> DELETE (CAPS+0)
  kbd_map[0x29] = KM_CAPS | zx_code_of(' ');   // Esc       -> BREAK  (CAPS+SPACE)
  kbd_map[0x50] = KM_CAPS | zx_code_of('5');   // Влево     -> CAPS+5
  kbd_map[0x51] = KM_CAPS | zx_code_of('6');   // Вниз      -> CAPS+6
  kbd_map[0x52] = KM_CAPS | zx_code_of('7');   // Вверх     -> CAPS+7
  kbd_map[0x4F] = KM_CAPS | zx_code_of('8');   // Вправо    -> CAPS+8
  kbd_map[0x39] = KM_CAPS | zx_code_of('2');   // Caps Lock -> CAPS LOCK (CAPS+2)
  kbd_map[0x2B] = KM_CAPS | KM_SYM | 0xFF;     // Tab       -> EXTEND MODE (CAPS+SYM)
}

static inline void zx_press(uint8_t m[8], uint8_t code) {
  if (code != 0xFF) m[code >> 3] &= (uint8_t)~(1 << (code & 7));
}

// Claude: Колбэк библиотеки: пришёл новый отчёт клавиатуры (модификаторы + до 6 клавиш).
//   Вызывается из USBHost.task() в loop(), то есть на ядре 1 — там же, где Z80.
//   Матрицу строим каждый раз с нуля: отпускание клавиш получается автоматически.
static void on_kbd_report(uint8_t mods, const uint8_t keys[6], void *) {
  for (int i = 0; i < 6; i++) if (keys[i] == 0x01) return;   // "слишком много клавиш" — игнор
  // --- Claude: меню дисков: берём только что нажатую клавишу, Спектрум ничего не получает ---
  static uint8_t prev_keys[6] = {0, 0, 0, 0, 0, 0};
  // Claude (01.10.2026): Ctrl+Alt+End — проверяем ДО меню, чтобы работало и в меню F9.
  {
    bool end_now = false;
    for (int i = 0; i < 6; i++) if (keys[i] == 0x4D) end_now = true;   // End
    if (end_now && (mods & (USBHOST_KEY_MOD_LEFT_CTRL | USBHOST_KEY_MOD_RIGHT_CTRL)) &&
                   (mods & (USBHOST_KEY_MOD_LEFT_ALT  | USBHOST_KEY_MOD_RIGHT_ALT))) {
      hard_request = true;
      return;
    }
  }
  if (menu_active) {
    // Claude (01.10): ЧТО: кроме новой клавиши запоминаем, какая клавиша навигации держится.
    //   ПОЧЕМУ здесь: клавиатура шлёт отчёт только при изменении, повторов сама не даёт —
    //   автоповтор считает меню по времени, пока держится menu_hold.
    uint8_t hold = 0;
    for (int i = 0; i < 6; i++) {
      if (!keys[i]) continue;
      bool was = false;
      for (int j = 0; j < 6; j++) if (prev_keys[j] == keys[i]) was = true;
      if (!was) menu_key = keys[i];
      bool nav = keys[i] == 0x52 || keys[i] == 0x51 || keys[i] == 0x4B || keys[i] == 0x4E;
      if (nav && (!hold || !was)) hold = keys[i];          // новая стрелка важнее давно зажатой
    }
    menu_hold = hold;
    memcpy(prev_keys, keys, 6);
    for (int r = 0; r < 8; r++) zx_keys[r] = 0x1F;
    return;
  }
  memcpy(prev_keys, keys, 6);

  // --- Claude: служебные клавиши (срабатывают по моменту нажатия, а не пока держим) ---
  bool f9_now = false, f10_now = false, f11_now = false, f12_now = false, del_now = false, any_now = (mods != 0);
  for (int i = 0; i < 6; i++) {
    if (keys[i]) any_now = true;
    if (keys[i] == 0x42) f9_now = true;       // F9  — меню дисков
    if (keys[i] == 0x43) f10_now = true;      // F10 — Сброс
    if (keys[i] == 0x44) f11_now = true;      // F11 — Magic (NMI)
    if (keys[i] == 0x45) f12_now = true;      // F12 — лента: перемотка + LOAD ""
    if (keys[i] == 0x4C) del_now = true;      // Delete
  }
  // Claude: Ctrl+Alt+Del — тоже сброс (Ctrl и Alt любые, левые или правые).
  bool cad_now = del_now &&
                 (mods & (USBHOST_KEY_MOD_LEFT_CTRL | USBHOST_KEY_MOD_RIGHT_CTRL)) &&
                 (mods & (USBHOST_KEY_MOD_LEFT_ALT  | USBHOST_KEY_MOD_RIGHT_ALT));
  static bool f9_prev = false, f10_prev = false, f11_prev = false, f12_prev = false, cad_prev = false;
  // Claude: После сброса клавиатура для Спектрума "глухая", пока не отпущены ВСЕ клавиши:
  //   иначе Ctrl/Alt (= SYMBOL SHIFT) были бы зажаты во время старта ROM, а Скорпион
  //   по зажатым при сбросе клавишам делает особую (полную) инициализацию.
  static bool kbd_block = false;
  if ((f10_now && !f10_prev) || (cad_now && !cad_prev)) { f10_request = true; kbd_block = true; }
  if (f11_now && !f11_prev) f11_request = true;
  if (f9_now && !f9_prev) f9_request = true;
  if (f12_now && !f12_prev) f12_request = true;
  f9_prev = f9_now; f10_prev = f10_now; f11_prev = f11_now; f12_prev = f12_now; cad_prev = cad_now;
  if (kbd_block && !any_now) kbd_block = false;   // всё отпустили — снова слышим клавиатуру

  // --- Матрица Спектрума: строим с нуля — отпускание получается автоматически ---
  uint8_t m[8];
  memset(m, 0x1F, sizeof(m));
  if (!kbd_block) {
    if (mods & (USBHOST_KEY_MOD_LEFT_SHIFT | USBHOST_KEY_MOD_RIGHT_SHIFT)) zx_press(m, ZX_CAPS);
    if (mods & (USBHOST_KEY_MOD_LEFT_CTRL | USBHOST_KEY_MOD_RIGHT_CTRL |
                USBHOST_KEY_MOD_LEFT_ALT  | USBHOST_KEY_MOD_RIGHT_ALT)) zx_press(m, ZX_SYM);
    for (int i = 0; i < 6; i++) {
      uint16_t e = kbd_map[keys[i]];
      if (e & KM_CAPS) zx_press(m, ZX_CAPS);
      if (e & KM_SYM)  zx_press(m, ZX_SYM);
      zx_press(m, (uint8_t)(e & 0xFF));
    }
  }
  for (int r = 0; r < 8; r++) zx_keys[r] = m[r];
  // Claude: Отладка — видно, что приходит от клавиатуры. Потом можно убрать.
  Serial.printf("[kbd] mods=%02X keys=%02X %02X %02X %02X %02X %02X%s\n",
                mods, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5], kbd_block ? " (blocked)" : "");
}

static USBHostHIDKeyboard usb_kbd;   // Claude: библиотека требует, чтобы экземпляр объявил скетч

static void keyboard_begin() {
  build_kbd_map();
  usb_kbd.setLockHandling(false);           // Claude: Caps Lock — наша клавиша, светодиоды не трогаем
  usb_kbd.setNotifyOnChangeOnly(true);      // Claude: колбэк только при изменении нажатий
  usb_kbd.setReportCallback(on_kbd_report, nullptr);
  usb_kbd.registerWithHost();               // Claude: строго ДО USBHost.begin()
  // Claude: Упрощённый "BIOS"-режим (boot protocol): 8-байтовые отчёты. Для NKRO-клавиатур
  //   (как ARDOR Katana) без этого приходил бы их собственный расширенный формат.
  tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);
  USBHost.setCore(0);                       // Claude: USB-стек на ядре 0, рядом с видео; Z80 не трогаем
  if (USBHost.begin()) Serial.println("USB host running (core 0). Plug keyboard into USB port.");
  else                 Serial.println("ERR: USBHost.begin() failed");
}

// Claude: Раз в кадр из loop(): раздать колбэки и отследить подключение/отключение.
static void keyboard_poll() {
  static bool was_mounted = false;
  USBHost.task();
  bool now_mounted = usb_kbd.mounted();
  if (now_mounted != was_mounted) {
    was_mounted = now_mounted;
    Serial.println(now_mounted ? "[kbd] keyboard connected" : "[kbd] keyboard disconnected");
    // Claude: Библиотека при отключении НЕ обнуляет последние клавиши — отпускаем всё сами,
    //   иначе выдернутый приёмник с зажатой клавишей оставил бы её "залипшей".
    if (!now_mounted) for (int r = 0; r < 8; r++) zx_keys[r] = 0x1F;
  }
}

// ==== AUDIO BEGIN (Claude: синтез AY + бипер; прогоняется тестом на ПК) ====
// ---------------------------------------------------------------------------
// Claude: ЗВУК. Во время кадра запись в AY и переключение бипера кладутся в очередь
//   событий с моментом в тактах. В конце кадра audio_render_frame() "проигрывает" кадр:
//   идёт шагами по 16 тактов Z80 (= 8 тактов AY при 1.75 МГц — это "тик" AY), применяет
//   события в свои моменты, считает AY и бипер и усредняет тики в 625 сэмплов.
//   Усреднение — простой фильтр от "зубцов" при высоких частотах.
//
//   AY-3-8912 (по устройству реальной микросхемы):
//     тон:   счётчик на тиках clock/8, переключение каждые TP тиков -> f = clock/(16*TP)
//     шум:   17-битный LFSR (отводы 0 и 3), шаг каждые NP тиков clock/16
//     огибающая: 16 шагов, шаг каждые EP тиков clock/16 -> цикл clock/(256*EP);
//                формы по R13 (логика attack/hold/alternate как в MAME ay8910)
//     выход канала = (тон | тон_выкл) & (шум | шум_выкл), громкость — лог. таблица AY.
// ---------------------------------------------------------------------------
struct AudioEv { uint32_t t; uint8_t kind, reg, val; };   // kind: 0 = бипер, 1 = запись в AY
static const int AEV_MAX = 4096;
static AudioEv aev[AEV_MAX];
static int aev_n = 0;

static inline void audio_event(int t, uint8_t kind, uint8_t reg, uint8_t val) {
  if (t < 0) t = 0;
  if (aev_n < AEV_MAX) { aev[aev_n].t = (uint32_t)t; aev[aev_n].kind = kind; aev[aev_n].reg = reg; aev[aev_n].val = val; aev_n++; }
}

// Claude: Громкость AY 0..15 -> 0..255 (логарифмическая, по измерениям реальной микросхемы).
static const uint8_t AY_VOL[16] = {0, 3, 5, 7, 10, 15, 21, 34, 40, 65, 91, 114, 144, 181, 215, 255};
static const int BEEP_LVL = 160;              // Claude: громкость бипера (канал AY на максимуме = 255)

static struct {
  uint8_t  r[16];                             // регистры "со стороны синтеза" (меняются по событиям)
  uint16_t tcnt[3]; uint8_t tout[3];
  uint16_t ncnt; uint32_t lfsr; uint8_t nout;
  uint16_t ecnt; int8_t estep; uint8_t eattack, ehold, ealt, eholding, evol;
  uint8_t  half, beep;
} ay;

static void ay_env_restart(uint8_t shape) {   // Claude: запись в R13 перезапускает огибающую
  ay.eattack = (shape & 0x04) ? 15 : 0;
  if (!(shape & 0x08)) { ay.ehold = 1; ay.ealt = ay.eattack; }   // "Continue=0" -> как 1 с hold
  else                 { ay.ehold = shape & 0x01; ay.ealt = shape & 0x02; }
  ay.estep = 15; ay.eholding = 0; ay.ecnt = 0;
  ay.evol = (uint8_t)(ay.estep ^ ay.eattack);
}

static void ay_reset() {
  memset(&ay, 0, sizeof(ay));
  ay.lfsr = 1;
  ay.r[7] = 0xFF;                             // всё выключено
  ay_env_restart(0);
  aev_n = 0;
}

// Claude: аргументы — простые числа, а не AudioEv: генератор прототипов Arduino вставляет
//   объявления функций в начало файла, раньше определения структуры, и сборка падала.
static inline void ay_apply(uint8_t kind, uint8_t reg, uint8_t val) {
  if (kind == 0) { ay.beep = val; return; }
  ay.r[reg & 15] = val;
  if ((reg & 15) == 13) ay_env_restart(val);
}

static inline void ay_tick() {
  for (int i = 0; i < 3; i++) {
    uint16_t tp = ay.r[2 * i] | ((ay.r[2 * i + 1] & 0x0F) << 8);
    if (!tp) tp = 1;
    if (++ay.tcnt[i] >= tp) { ay.tcnt[i] = 0; ay.tout[i] ^= 1; }
  }
  ay.half ^= 1;
  if (ay.half) {                              // шум и огибающая — на вдвое меньшей частоте
    uint16_t np = ay.r[6] & 0x1F; if (!np) np = 1;
    if (++ay.ncnt >= np) {
      ay.ncnt = 0;
      ay.lfsr = (ay.lfsr >> 1) | (((ay.lfsr ^ (ay.lfsr >> 3)) & 1) << 16);
      ay.nout = ay.lfsr & 1;
    }
    uint16_t ep = ay.r[11] | (ay.r[12] << 8); if (!ep) ep = 1;
    if (++ay.ecnt >= ep) {
      ay.ecnt = 0;
      if (!ay.eholding) {
        ay.estep--;
        if (ay.estep < 0) {
          if (ay.ehold) { if (ay.ealt) ay.eattack ^= 15; ay.eholding = 1; ay.estep = 0; }
          else          { if (ay.ealt) ay.eattack ^= 15; ay.estep &= 15; }
        }
      }
      ay.evol = (uint8_t)(ay.estep ^ ay.eattack);
    }
  }
}

static inline int ay_chan(int i) {
  uint8_t r7 = ay.r[7];
  int t_off = (r7 >> i) & 1, n_off = (r7 >> (i + 3)) & 1;
  int out = (ay.tout[i] | t_off) & (ay.nout | n_off);
  if (!out) return 0;
  uint8_t v = ay.r[8 + i];
  return AY_VOL[(v & 0x10) ? ay.evol : (v & 0x0F)];
}

// Claude: Синтез одного кадра: AUDIO_SPF сэмплов в outL/outR.
// Claude (01.10): ЧТО: на выходе "уровень" 0..542 (сумма каналов AY + бипер), без привязки
//   к способу вывода. ПОЧЕМУ: раньше тут сразу считалась плотность сигма-дельты (8 бит);
//   для I2S нужна полная точность, а перевод под каждый выход делает сам выход.
static void audio_render_frame(uint16_t *outL, uint16_t *outR) {
  const int TICKS = FRAME_TSTATES / 16;       // 4368 тиков AY на кадр
  int ev = 0, s = 0, cnt = 0;
  int32_t accL = 0, accR = 0;
  for (int k = 0; k < TICKS; k++) {
    uint32_t T = (uint32_t)k * 16;
    while (ev < aev_n && aev[ev].t <= T) { ay_apply(aev[ev].kind, aev[ev].reg, aev[ev].val); ev++; }
    ay_tick();
    int a = ay_chan(0), b = ay_chan(1), c = ay_chan(2);
    int bp = ay.beep ? BEEP_LVL : 0;
    int L, R;
#if AUDIO_MODE == 0
    L = R = ((a + b + c) >> 1) + bp;          // моно: всё в один канал
#elif AUDIO_MODE == 1
    L = a + (b >> 1) + bp;  R = c + (b >> 1) + bp;   // ABC: B посередине
#else
    L = a + (c >> 1) + bp;  R = b + (c >> 1) + bp;   // ACB: C посередине
#endif
    accL += L; accR += R; cnt++;
    int s_next = ((k + 1) * AUDIO_SPF) / TICKS;
    if (s_next > s) {                         // Claude: максимум 542 (255 + 127 + 160)
      outL[s] = (uint16_t)(accL / cnt);
      outR[s] = (uint16_t)(accR / cnt);
      s++; accL = accR = 0; cnt = 0;
    }
  }
  while (ev < aev_n) { ay_apply(aev[ev].kind, aev[ev].reg, aev[ev].val); ev++; }   // события после последнего тика (перелёт кадра)
  aev_n = 0;
}
// ==== AUDIO END ====

// ==== FDC BEGIN (Claude: дисковод — ВГ93 (WD1793) + системный регистр #FF; прогоняется тестом на ПК) ====
// ---------------------------------------------------------------------------
// Claude: ДИСКОВОД. Порты ВГ93 работают только при активном DOS/ и A0=A1=1:
//     #1F команда/статус, #3F дорожка, #5F сектор, #7F данные, #FF системный регистр.
//   Системный регистр (проверено по TR-DOS 5.03 из нашего ROM):
//     запись: D0-D1 дисковод A..D, D2=0 сброс ВГ93, D3 загрузка головки,
//             D4 сторона: 1 = нижняя (0), 0 = верхняя (1)  (#1FEB: OR #3C / #1FF6: AND #6F)
//     чтение: D7 = INTRQ (команда завершена), D6 = DRQ (нужен байт) — #3FCA: AND #C0 / RET M
//   Тайминги упрощены: данные готовы сразу, шаг головки мгновенный. Честно сделан только
//   индексный импульс (300 об/мин = раз в 698880 тактов = 10 кадров): по нему TR-DOS
//   определяет наличие диска (#3DB5: ждёт смены бита 1 статуса после Restore).
//   Физическое положение головки хранится отдельно от регистра дорожки: TR-DOS так
//   различает 40/80-дорожечные дисководы (#3E16: Seek + проверка датчика TR00).
//   Готовность (бит 7 статуса) = диск вставлен И крутится. Мотор крутится 15 оборотов
//   (3 с) после последней команды — как загрузка головки у ВГ93. Это видно по ROM:
//   TR-DOS перед командой запоминает бит 7 (#3EB5) и, если диск стоял, ждёт разгона
//   (#3E97); оболочка "128 TR-DOS" после Read Address ждёт остановки мотора
//   (ROM 2 #0237: пока статус & #E0 == 0). Вывод по коду ROM, не по схеме.
//   Образ диска — .trd (80 цил. x 2 стороны x 16 сект. x 256 байт) в PSRAM;
//   .scl при вставке разворачивается в .trd. Запись идёт в образ в памяти (до сброса питания).
// ---------------------------------------------------------------------------
#ifndef FDC_ALLOC
#define FDC_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM)
#endif
static const int      FDD_CYLS = 80, FDD_SECS = 16, FDD_SECSZ = 256;
static const uint32_t FDD_SIZE = 80u * 2 * 16 * 256;       // 655360 байт
static const uint32_t FDC_REV_T = 698880;                  // один оборот диска, тактов Z80
static const uint32_t FDC_INDEX_T = 14000;                 // длительность индексного импульса (~4 мс)
static const int      FDC_TRACK_BYTES = 6250;              // байт на дорожке MFM DD (для записи дорожки)
static const int64_t  FDC_MOTOR_T = 15LL * 698880;         // мотор после последней команды: 15 оборотов

static uint8_t *fdd_img[4] = {nullptr, nullptr, nullptr, nullptr};   // NULL = нет диска
static uint8_t  fdd_wp[4], fdd_dirty[4], fdd_cyl[4];       // защита записи, изменён, где головка
// Claude (01.10): ЧТО: какие дорожки образа изменены (160 бит = 80 цил. x 2 стороны) и кадр
//   последней записи. ПОЧЕМУ: сохранять на SD только изменённые дорожки (по 4 КБ), а не весь
//   образ 640 КБ, и не во время обмена, а после паузы.
static volatile uint32_t fdd_tdirty[4][5];
static volatile uint32_t fdd_wr_frame[4];
// Claude: Номер дорожки в ID-полях каждой дорожки. Обычно = цилиндр, но FORMAT проверяет
//   вторую сторону так: пишет на стороне 1 дорожку с ID "дорожка 1", на стороне 0 — с ID 0,
//   потом читает адрес со стороны 1 и ждёт 1 (#1EEE..#1F16). Поэтому храним, что записано.
static uint8_t  fdd_idtrk[4][80][2];

static uint8_t  fdc_drv = 0, fdc_side = 0;                 // из системного регистра
static uint8_t  fdc_st = 0, fdc_trk = 0, fdc_sec = 1, fdc_dat = 0;
static uint8_t  fdc_type1 = 1;       // 1 = статус в формате команд типа I (индекс, TR00)
static uint8_t  fdc_intrq = 0, fdc_drq = 0, fdc_multi = 0;
static int8_t   fdc_dir = 1;
static uint8_t  fdc_mode = 0;        // 0 нет, 1 чтение, 2 запись, 3 чтение адреса, 4 запись дорожки
static uint8_t *fdc_ptr = nullptr;
static int      fdc_pos = 0, fdc_len = 0;
static uint8_t  fdc_idbuf[6], fdc_idsec = 0;
static int      fdc_wt_state = 0, fdc_wt_cnt = 0, fdc_wt_need = 0;
static uint8_t  fdc_wt_prev = 0, fdc_wt_id[4];
static uint8_t *fdc_wt_dst = nullptr;
static int64_t  fdc_now = 0;         // "время" в тактах от старта (индекс, мотор)
static int64_t  fdc_act = -(1LL << 40);   // момент последней команды/обмена (мотор крутится)

static inline bool fdc_ready() { return fdd_img[fdc_drv] != nullptr; }        // диск вставлен
static inline bool fdc_spin()  { return fdc_ready() && fdc_now - fdc_act < FDC_MOTOR_T; }

static uint8_t *fdc_sector_ptr(int cyl, int side, int sec) {
  if (!fdc_ready() || cyl < 0 || cyl >= FDD_CYLS || sec < 1 || sec > FDD_SECS) return nullptr;
  return fdd_img[fdc_drv] + ((uint32_t)(cyl * 2 + side) * FDD_SECS + (sec - 1)) * FDD_SECSZ;
}

static uint16_t fdc_crc(uint16_t crc, uint8_t b) {         // CRC-CCITT, как у ВГ93
  crc ^= (uint16_t)b << 8;
  for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  return crc;
}

static void fdc_done(uint8_t st) { fdc_st = st; fdc_drq = 0; fdc_mode = 0; fdc_intrq = 1; }

static void fdc_reset() {
  fdc_st = 0; fdc_intrq = 0; fdc_drq = 0; fdc_mode = 0; fdc_type1 = 1; fdc_sec = 1;
}

static void fdc_step(int dir, bool upd) {
  fdc_dir = (int8_t)dir;
  int c = fdd_cyl[fdc_drv] + dir;
  if (c < 0) c = 0;
  if (c > 85) c = 85;                  // упор механики
  fdd_cyl[fdc_drv] = (uint8_t)c;
  if (upd) fdc_trk = (uint8_t)(fdc_trk + dir);
}

static void fdc_command(uint8_t v) {
  if ((v & 0xF0) == 0xD0) {                                // тип IV: Force Interrupt
    fdc_mode = 0; fdc_drq = 0; fdc_type1 = 1;
    fdc_st &= 0x20;
    fdc_intrq = (v & 0x0F) ? 1 : 0;
    return;
  }
  if (fdc_mode) return;                                    // занят — ВГ93 игнорирует команду
  fdc_act = fdc_now;                                       // команда запускает мотор
  fdc_intrq = 0; fdc_drq = 0;
  if (!(v & 0x80)) {                                       // тип I: Restore/Seek/Step
    fdc_type1 = 1;
    uint8_t op = v >> 4;
    if (op == 0) {                                         // Restore
      fdd_cyl[fdc_drv] = 0; fdc_trk = 0;
    } else if (op == 1) {                                  // Seek (цель — в регистре данных)
      while (fdc_trk != fdc_dat) fdc_step(fdc_dat > fdc_trk ? 1 : -1, true);
    } else if (op <= 3) fdc_step(fdc_dir, op & 1);         // Step
    else if (op <= 5)   fdc_step(1, op & 1);               // Step In
    else                fdc_step(-1, op & 1);              // Step Out
    uint8_t st = (v & 0x08) ? 0x20 : 0;                    // h: головка загружена
    if ((v & 0x04) && (!fdc_ready() || fdd_cyl[fdc_drv] != fdc_trk || fdc_trk >= FDD_CYLS))
      st |= 0x10;                                          // V: ошибка поиска
    fdc_done(st);
    return;
  }
  fdc_type1 = 0;
  if (!fdc_ready()) { fdc_done(0x80); return; }            // нет диска: "не готов"
  int cyl = fdd_cyl[fdc_drv];
  if ((v & 0xC0) == 0x80) {                                // тип II: чтение/запись сектора
    fdc_multi = (v & 0x10) ? 1 : 0;
    bool wr = v & 0x20;
    // ID-поле сектора содержит физический цилиндр: при расхождении с регистром дорожки —
    // "сектор не найден" (так TR-DOS замечает сбившуюся головку и перекалибрует)
    uint8_t *p = (cyl < FDD_CYLS && fdd_idtrk[fdc_drv][cyl][fdc_side] == fdc_trk)
                 ? fdc_sector_ptr(cyl, fdc_side, fdc_sec) : nullptr;
    if (!p) { fdc_done(0x10); return; }
    if (wr && fdd_wp[fdc_drv]) { fdc_done(0x40); return; }
    fdc_ptr = p; fdc_pos = 0; fdc_len = FDD_SECSZ;
    fdc_mode = wr ? 2 : 1; fdc_drq = 1; fdc_st = 0x03;
    return;
  }
  if ((v & 0xF0) == 0xC0) {                                // тип III: чтение адреса
    if (cyl >= FDD_CYLS) { fdc_done(0x10); return; }
    fdc_idsec = (uint8_t)((fdc_idsec % FDD_SECS) + 1);     // "диск вращается": следующий сектор
    fdc_idbuf[0] = fdd_idtrk[fdc_drv][cyl][fdc_side]; fdc_idbuf[1] = fdc_side; fdc_idbuf[2] = fdc_idsec; fdc_idbuf[3] = 1;
    uint16_t crc = 0xFFFF;
    crc = fdc_crc(crc, 0xA1); crc = fdc_crc(crc, 0xA1); crc = fdc_crc(crc, 0xA1); crc = fdc_crc(crc, 0xFE);
    for (int i = 0; i < 4; i++) crc = fdc_crc(crc, fdc_idbuf[i]);
    fdc_idbuf[4] = (uint8_t)(crc >> 8); fdc_idbuf[5] = (uint8_t)crc;
    fdc_sec = fdc_idbuf[0];                                // ВГ93 кладёт номер дорожки из ID в регистр сектора
    fdc_ptr = fdc_idbuf; fdc_pos = 0; fdc_len = 6;
    fdc_mode = 3; fdc_drq = 1; fdc_st = 0x03;
    return;
  }
  if ((v & 0xF0) == 0xF0) {                                // тип III: запись дорожки (форматирование)
    if (fdd_wp[fdc_drv]) { fdc_done(0x40); return; }
    fdc_wt_state = 0; fdc_wt_cnt = 0; fdc_wt_prev = 0; fdc_pos = 0;
    fdc_mode = 4; fdc_drq = 1; fdc_st = 0x03;
    return;
  }
  fdc_done(0);   // 0xE0 чтение дорожки — не поддержано (TR-DOS не использует): сразу "готово"
}

// Claude: Разбор потока записи дорожки (MFM): F5 = синхробайт A1, FE = ID-поле (4 байта),
//   FB/F8 = поле данных, F7 = записать CRC. Данные кладём в сектор с номером из ID-поля.
static void fdc_write_track_byte(uint8_t b) {
  if (fdc_wt_state == 0) {
    if (fdc_wt_prev == 0xF5 && b == 0xFE) { fdc_wt_state = 1; fdc_wt_need = 0; }
    else if (fdc_wt_prev == 0xF5 && (b == 0xFB || b == 0xF8)) {
      int len = 128 << (fdc_wt_id[3] & 3);
      fdc_wt_dst = (len == FDD_SECSZ) ? fdc_sector_ptr(fdd_cyl[fdc_drv], fdc_side, fdc_wt_id[2]) : nullptr;
      fdc_wt_state = 2; fdc_wt_need = len; fdc_wt_cnt = 0;
    }
  } else if (fdc_wt_state == 1) {
    fdc_wt_id[fdc_wt_need++] = b;
    if (fdc_wt_need == 4) {
      fdc_wt_state = 0;
      if (fdd_cyl[fdc_drv] < FDD_CYLS) fdd_idtrk[fdc_drv][fdd_cyl[fdc_drv]][fdc_side] = fdc_wt_id[0];
    }
  } else {
    if (fdc_wt_dst) fdc_wt_dst[fdc_wt_cnt] = b;
    if (++fdc_wt_cnt >= fdc_wt_need) fdc_wt_state = 0;
  }
  fdc_wt_prev = b;
}

static uint8_t fdc_read_data() {
  if (fdc_mode == 1 || fdc_mode == 3) {
    fdc_act = fdc_now;
    uint8_t b = fdc_ptr[fdc_pos];
    fdc_dat = b;
    if (++fdc_pos >= fdc_len) {
      if (fdc_mode == 1 && fdc_multi) {                    // многосекторное: до конца дорожки
        fdc_sec++;
        uint8_t *p = fdc_sector_ptr(fdd_cyl[fdc_drv], fdc_side, fdc_sec);
        if (p) { fdc_ptr = p; fdc_pos = 0; } else fdc_done(0x10);
      } else fdc_done(0);
    }
    return b;
  }
  return fdc_dat;
}

static inline void fdc_mark_dirty() {                   // Claude (01.10): см. fdd_tdirty
  int tr = fdd_cyl[fdc_drv] * 2 + fdc_side;
  // Claude (01.10): атомарно — флажок снимает задача сохранения на другом ядре
  if (tr < FDD_CYLS * 2) __atomic_fetch_or(&fdd_tdirty[fdc_drv][tr >> 5], 1u << (tr & 31), __ATOMIC_RELAXED);
  fdd_wr_frame[fdc_drv] = zx_frame;
}

static void fdc_write_data(uint8_t v) {
  fdc_dat = v;
  if (fdc_mode) fdc_act = fdc_now;
  if (fdc_mode == 2) {
    fdc_ptr[fdc_pos] = v;
    fdd_dirty[fdc_drv] = 1;
    fdc_mark_dirty();
    if (++fdc_pos >= fdc_len) {
      if (fdc_multi) {
        fdc_sec++;
        uint8_t *p = fdc_sector_ptr(fdd_cyl[fdc_drv], fdc_side, fdc_sec);
        if (p) { fdc_ptr = p; fdc_pos = 0; } else fdc_done(0x10);
      } else fdc_done(0);
    }
  } else if (fdc_mode == 4) {
    fdc_write_track_byte(v);
    fdd_dirty[fdc_drv] = 1;
    fdc_mark_dirty();
    if (++fdc_pos >= FDC_TRACK_BYTES) fdc_done(0);       // дорожка кончилась — до индексного импульса
  }
}

static uint8_t fdc_status() {
  fdc_intrq = 0;
  uint8_t s;
  if (fdc_type1) {
    bool index = fdc_spin() && (fdc_now % FDC_REV_T) < FDC_INDEX_T;
    s = (uint8_t)((fdc_st & 0x39) | (fdc_spin() ? 0 : 0x80) | (fdd_wp[fdc_drv] ? 0x40 : 0) |
                  (fdd_cyl[fdc_drv] == 0 ? 0x04 : 0) | (index ? 0x02 : 0));
  } else {
    s = (uint8_t)((fdc_st & 0x7D) | (fdc_drq ? 0x02 : 0) | (fdc_spin() ? 0 : 0x80));
  }
  return s;
}

// Claude: Время и "брошенные" команды: если обмен данными не идёт дольше оборота диска,
//   ВГ93 завершает команду с потерей данных (бит 2) — как реальный контроллер.
static void fdc_time(int t) {
  fdc_now = (int64_t)zx_frame * FRAME_TSTATES + t;
  if (fdc_mode && fdc_now - fdc_act > (int64_t)FDC_REV_T) fdc_done(fdc_mode == 4 ? 0 : 0x04);
}

// Claude: Вызываются из zx_port_in/zx_port_out при DOS/ (или #1FFD.D1) и A0=A1=1. t — такт кадра.
static uint8_t fdc_in(uint8_t lo, int t) {
  fdc_time(t);
  if (lo & 0x80) return (uint8_t)((fdc_intrq ? 0x80 : 0) | (fdc_drq ? 0x40 : 0) | 0x3F);   // #FF
  switch ((lo >> 5) & 3) {
    case 0:  return fdc_status();                          // #1F
    case 1:  return fdc_trk;                               // #3F
    case 2:  return fdc_sec;                               // #5F
    default: return fdc_read_data();                       // #7F
  }
}

static void fdc_out(uint8_t lo, uint8_t v, int t) {
  fdc_time(t);
  if (lo & 0x80) {                                         // #FF: системный регистр
    fdc_drv = v & 3;
    fdc_side = (v & 0x10) ? 0 : 1;
    if (!(v & 0x04)) fdc_reset();                          // D2=0 — сброс ВГ93
    return;
  }
  switch ((lo >> 5) & 3) {
    case 0:  fdc_command(v); break;                        // #1F
    case 1:  fdc_trk = v; break;                           // #3F
    case 2:  fdc_sec = v; break;                           // #5F
    default: fdc_write_data(v); break;                     // #7F
  }
}

// Claude: Вставить диск в дисковод d (0..3). Данные — .trd (любой длины до 640 КБ)
//   или .scl (узнаётся по сигнатуре SINCLAIR). Образ копируется в PSRAM.
//   Возвращает true, если получилось.
static bool fdd_insert(int d, const uint8_t *data, uint32_t len, bool wp) {
  if (d < 0 || d > 3) return false;
  if (!fdd_img[d]) fdd_img[d] = (uint8_t *)FDC_ALLOC(FDD_SIZE);
  uint8_t *img = fdd_img[d];
  if (!img) return false;
  memset(img, 0, FDD_SIZE);
  if (len >= 9 && memcmp(data, "SINCLAIR", 8) == 0) {
    // .scl: "SINCLAIR", число файлов N, N заголовков по 14 байт, потом данные файлов подряд.
    //   Раскладываем как TR-DOS: каталог — дорожка 0, секторы 1..8 (по 16 байт на файл:
    //   14 байт заголовка + сектор + дорожка), служебный сектор 9, файлы — с дорожки 1.
    int n = data[8];
    uint32_t src = 9 + 14u * n;
    uint32_t pos = 16;                                     // логический сектор (дорожка 1, сектор 0)
    if (n > 128 || src > len) { free(img); fdd_img[d] = nullptr; return false; }
    for (int i = 0; i < n; i++) {
      const uint8_t *h = data + 9 + 14 * i;
      uint32_t secs = h[13];
      if (pos + secs > 2560 || src + secs * 256 > len) { free(img); fdd_img[d] = nullptr; return false; }
      memcpy(img + i * 16, h, 14);
      img[i * 16 + 14] = (uint8_t)(pos % 16);
      img[i * 16 + 15] = (uint8_t)(pos / 16);
      memcpy(img + pos * 256, data + src, secs * 256);
      src += secs * 256; pos += secs;
    }
    uint8_t *s9 = img + 8 * 256;                           // служебный сектор (сектор 9 дорожки 0)
    s9[0xE1] = (uint8_t)(pos % 16);                        // первый свободный сектор
    s9[0xE2] = (uint8_t)(pos / 16);                        // первая свободная дорожка
    s9[0xE3] = 0x16;                                       // тип: 80 дорожек, 2 стороны
    s9[0xE4] = (uint8_t)n;                                 // файлов
    s9[0xE5] = (uint8_t)((2560 - pos) & 0xFF);             // свободных секторов
    s9[0xE6] = (uint8_t)((2560 - pos) >> 8);
    s9[0xE7] = 0x10;                                       // признак TR-DOS
    memset(s9 + 0xEA, ' ', 9);
    memset(s9 + 0xF5, ' ', 8);                             // метка диска
  } else {
    memcpy(img, data, len > FDD_SIZE ? FDD_SIZE : len);
  }
  for (int c = 0; c < FDD_CYLS; c++) fdd_idtrk[d][c][0] = fdd_idtrk[d][c][1] = (uint8_t)c;
  fdd_wp[d] = wp ? 1 : 0;
  fdd_dirty[d] = 0;
  for (int i = 0; i < 5; i++) fdd_tdirty[d][i] = 0;       // Claude (01.10): новый образ — чистый
  return true;
}
// Claude: Вынуть диск из дисковода d (память образа освобождается).
static void fdd_eject(int d) {
  if (d < 0 || d > 3 || !fdd_img[d]) return;
  if (d == fdc_drv && fdc_mode) fdc_done(0x80);          // обмен с этим диском — оборвать
  free(fdd_img[d]);
  fdd_img[d] = nullptr;
  fdd_dirty[d] = 0;
}
// ==== FDC END ====

// ==== PORTS BEGIN (Claude: порты Скорпиона; прогоняется тестом на ПК) ====
// ---------------------------------------------------------------------------
// Claude: Декодирование портов — по маскам выбора из руководства (с исправлениями из
//   списка ошибок издания 2009 г.). Порт приходит полным 16-битным адресом.
//     #7FFD: A0,A2,A5,A12,A14=1; A1,A15=0      -> маска #D027, значение #5025
//     #1FFD: A0,A2,A5,A12=1;  A1,A14,A15=0     -> маска #D027, значение #1025
//     AY:    A0,A2,A5,A15=1;  A1=0; A14: 1=#FFFD (регистр), 0=#BFFD (данные)
//   Порты, оканчивающиеся на FD (A1=0), работают и при активном DOS/; остальные в DOS
//   отключаются, вместо них — порты ВГ93 (A0=A1=1: #1F,#3F,#5F,#7F,#FF).
// ---------------------------------------------------------------------------
extern "C" unsigned char zx_port_in(unsigned int port, int t) {   // Claude: t — такт кадра
  uint8_t lo = (uint8_t)port;
  if ((port & 0x8027) == 0x8025) return ay_reg[ay_sel];            // AY: чтение регистра
  if ((port & 0xD027) == 0x5025 || (port & 0xD027) == 0x1025)
    return 0xFF;   // Claude: чтение #7FFD/#1FFD: монитор так переключает турбо — игнорируем
  // Claude: ВГ93 (#1F..#FF) доступен при DOS/ и ещё при включённом #1FFD.D1 (ROM 2):
  //   оболочка "128 TR-DOS" работает из ОЗУ (DOS/ выключен) и опрашивает статус ВГ93
  //   через ROM 2 (#0237: IN A,(#1F) / AND H / RET M). Вывод по коду ROM, не по схеме.
  if ((zx_dos || (p1ffd & 0x02)) && (lo & 0x03) == 0x03) return fdc_in(lo, t);
  if (zx_dos) return 0xFF;
  if ((lo & 0x27) == 0x26)                                          // #FE: клавиатура
    return (uint8_t)(kbd_read((uint8_t)(port >> 8)) | 0xE0);        // бит 6 (лента) = 1
  if ((lo & 0x27) == 0x07) return 0x00;     // #1F: Kempston-джойстик, ничего не нажато
  return 0xFF;                              // #FF (атрибуты) и несуществующие: "плавающая шина"
}

extern "C" void zx_port_out(unsigned int port, unsigned char v, int t) {
  if ((port & 0xD027) == 0x5025) {                                  // #7FFD
    if (!(p7ffd & 0x20)) { p7ffd = v; zx_remap(); }                 // D5 = блокировка до сброса
    return;
  }
  if ((port & 0xD027) == 0x1025) { p1ffd = v; zx_remap(); return; } // #1FFD
  if ((port & 0x8027) == 0x8025) {                                  // AY
    if (port & 0x4000) { ay_sel = v & 0x0F; return; }               // #FFFD: номер регистра
    // #BFFD: данные. Claude: неиспользуемые биты регистров у AY не хранятся — маскируем,
    //   как в реальной микросхеме (программы иногда читают регистр обратно).
    static const uint8_t AY_MASK[16] = {0xFF,0x0F,0xFF,0x0F,0xFF,0x0F,0x1F,0xFF,
                                        0x1F,0x1F,0x1F,0xFF,0xFF,0x0F,0xFF,0xFF};
    v &= AY_MASK[ay_sel];
    ay_reg[ay_sel] = v;
    if (ay_sel < 14) audio_event(t, 1, ay_sel, v);                  // 14/15 — порты ввода-вывода AY
    return;
  }
  if ((zx_dos || (p1ffd & 0x02)) && (port & 0x03) == 0x03) {   // Claude: ВГ93, см. zx_port_in
    fdc_out((uint8_t)port, v, t);
    return;
  }
  if (zx_dos) return;       // Claude: прочие порты (#FE) при DOS/ отключены
  if (!(port & 0x01)) {                                             // #FE: бордюр, бипер
    zx_border = v & 0x07;
    uint8_t b = (v >> 4) & 0x01;
    if (b != zx_beeper) audio_event(t, 0, 0, b);                    // Claude: бипер — событие звука
    zx_beeper = b;
  }
}
// ==== PORTS END ====

// ==== TAPE BEGIN (Claude: этот кусок также прогнан тестом на ПК) ====
// ---------------------------------------------------------------------------
// Claude: ЛЕНТА. Перехват ROM-процедуры LD-BYTES (#0556).
//   На входе LD-BYTES: A = ожидаемый флаг блока (0 = заголовок, FF = данные),
//   IX = куда грузить, DE = сколько байт, флаг C: 1 = LOAD, 0 = VERIFY.
//   Мы берём следующий блок ленты, сверяем флаг, кладём данные и передаём управление
//   на SA/LD-RET (#053F) — как это делает сам ROM (LD-BYTES кладёт #053F в стек).
//   SA/LD-RET восстанавливает бордюр, делает EI, проверяет BREAK и возвращается
//   к вызвавшему с нашим флагом C (1 = успех). Так работает и BREAK (Esc).
//   Блок с чужим флагом просто "проматывается" с неуспехом — ROM сам ищет дальше,
//   точно как с настоящей лентой.
// ---------------------------------------------------------------------------
static const uint16_t TAPE_TRAP_ADDR = 0x0556;   // LD-BYTES
static const uint16_t SA_LD_RET      = 0x053F;
#ifdef TAPE_NAME                                  // Claude (01.10): tape.h есть — лента вставлена
static const uint8_t *tape_data = tape_image;
static uint32_t       tape_len  = sizeof(tape_image);
#else                                             // ленты нет: перехват отвечает "блока нет"
static const uint8_t *tape_data = nullptr;
static uint32_t       tape_len  = 0;
#endif
static uint32_t       tape_pos  = 0;
static int            tape_block_no = 0;

static void tape_rewind() { tape_pos = 0; tape_block_no = 0; }

static void tape_install_trap() {
  // Claude: ED 00 — неопределённая команда; z80emu на ней останавливается (z80config.h).
  //   Меняем только КОПИЮ страницы ROM 1 (Basic 48) в ОЗУ; исходный ROM во flash не трогаем.
  rom1_ram[TAPE_TRAP_ADDR]     = 0xED;
  rom1_ram[TAPE_TRAP_ADDR + 1] = 0x00;
}

static void tape_trap() {
  uint8_t  want_flag = z80.registers.byte[Z80_A];
  bool     is_load   = z80.registers.byte[Z80_F] & Z80_C_FLAG;
  uint16_t de = z80.registers.word[Z80_DE];
  uint16_t ix = z80.registers.word[Z80_IX];
  bool ok = false;

  if (tape_len >= 2) {
    if (tape_pos + 2 > tape_len) tape_rewind();            // Claude: конец ленты -> перемотка
    uint16_t blen = tape_data[tape_pos] | (tape_data[tape_pos + 1] << 8);
    const uint8_t *b = &tape_data[tape_pos + 2];
    int no = tape_block_no;
    tape_pos += 2 + blen;
    tape_block_no++;
    if (tape_pos >= tape_len) tape_rewind();

    if (blen >= 2 && b[0] == want_flag) {
      uint16_t dlen = blen - 2;                             // без флага и контрольной суммы
      uint16_t n = (de < dlen) ? de : dlen;
      bool same = true;
      for (uint16_t i = 0; i < n; i++, ix++) {
        uint8_t v = b[1 + i];
        if (is_load) zx_poke(ix, v);                        // ROM не перезаписывается (zx_poke)
        else if (zx_peek(ix) != v) same = false;            // VERIFY
      }
      de -= n;
      ok = (n == dlen) && (de == 0) && same;                // длина совпала точно
    }
    Serial.printf("[tape] block %d: flag=%02X len=%u want=%02X -> %s\n",
                  no, blen ? b[0] : 0, (unsigned)(blen >= 2 ? blen - 2 : 0), want_flag,
                  ok ? "OK" : "skip");
  }

  z80.registers.word[Z80_DE] = de;
  z80.registers.word[Z80_IX] = ix;
  if (ok) z80.registers.byte[Z80_F] |= Z80_C_FLAG;
  else    z80.registers.byte[Z80_F] &= (uint8_t)~Z80_C_FLAG;
  z80.pc = SA_LD_RET;
}

// ---------------------------------------------------------------------------
// Claude: АВТОНАБОР. Последовательность "нажатий" Спектрума: 5 кадров нажато,
//   10 кадров отпущено на каждую. Отпускание ДОЛЖНО быть дольше 5 кадров: ROM после
//   отпускания ещё ~5 прерываний держит клавишу в буфере KSTATE (антидребезг), и
//   повтор той же клавиши раньше срока теряется (проверено: при 3 кадрах вторая "
//   пропадала). Сейчас последовательность одна: LOAD "" + ENTER (режим K: J = LOAD).
// ---------------------------------------------------------------------------
static uint16_t inj_seq[8];
static int inj_len = 0, inj_idx = 0, inj_phase = 0;

static void inj_start_load() {
  inj_seq[0] = zx_code_of('J');                 // LOAD
  inj_seq[1] = KM_SYM | zx_code_of('P');        // "
  inj_seq[2] = KM_SYM | zx_code_of('P');        // "
  inj_seq[3] = zx_code_of('\n');                // ENTER
  inj_len = 4; inj_idx = 0; inj_phase = 0;
}

static void inj_step() {                        // Claude: раз в кадр
  uint8_t m[8];
  memset(m, 0x1F, sizeof(m));
  if (inj_idx < inj_len) {
    if (inj_phase < 5) {
      uint16_t e = inj_seq[inj_idx];
      if (e & KM_CAPS) zx_press(m, ZX_CAPS);
      if (e & KM_SYM)  zx_press(m, ZX_SYM);
      zx_press(m, (uint8_t)(e & 0xFF));
    }
    if (++inj_phase >= 15) { inj_phase = 0; inj_idx++; }
  }
  for (int r = 0; r < 8; r++) inj_keys[r] = m[r];
}

// ---------------------------------------------------------------------------
// Claude: ОДИН КАДР СПЕКТРУМА (69888 тактов). Вынесено в функцию, чтобы тест на ПК
//   выполнял буквально этот же код.
// ---------------------------------------------------------------------------
static int frame_carry = 0;    // "перелёт" тактов прошлого кадра (инструкция не режется)
static int magic_frames = 0;   // Claude: >0 — Magic нажата, ждём выполнения кода из ОЗУ (кадров осталось)

static void magic_press() { magic_frames = 100; }   // Claude: ждём до 2 секунд

// Claude (01.10.2026): СНИМОК ЭКРАНА ПО ЛУЧУ (см. fb_pix). Копирует в буфер fb_back все строки,
//   до которых луч дошёл к такту t (от прерывания). Строка ln (0..239) рисуется лучом с такта
//   FB_T_PAPER + (ln - FB_TOP) * FB_T_LINE: 24 строки бордюра над бумагой, 192 бумаги, 24 под.
//   Берём состояние в НАЧАЛЕ строки — точность до строки (изменения посреди строки не видны).
static int fb_ln = 0;                                  // следующая строка для снимка
static inline int fb_line_t(int ln) { return FB_T_PAPER + (ln - FB_TOP) * FB_T_LINE; }
static void fb_capture_upto(int t) {
  while (fb_ln < FB_LINES && t >= fb_line_t(fb_ln)) {
    fb_bord[fb_back][fb_ln] = zx_border;
    int L = fb_ln - FB_TOP;
    if ((unsigned)L < 192) {
      const uint8_t *scr = zx_screen;                  // экран 5 или 7 — какой виден в этот момент
      int a = ((L & 0xC0) << 5) | ((L & 0x07) << 8) | ((L & 0x38) << 2);
      memcpy(&fb_pix[fb_back][a], scr + a, 32);
      memcpy(&fb_att[fb_back][L * 32], scr + 0x1800 + ((L >> 3) << 5), 32);
    }
    fb_ln++;
  }
}
// Claude (01.10.2026): кадр готов — отдать его VGA. Новый буфер для записи — тот, который
//   сейчас не показывается и не ждёт показа (буферов три). Если VGA не успела забрать
//   прошлый готовый кадр, он просто заменяется новым.
// Claude (01.10.2026): ИСПРАВЛЕНО (v21). Было: fb_ready читался второй раз ПОСЛЕ замка.
//   Если прерывание VGA (ядро 0) успевало между этим забрать кадр (fb_ready = -1), выходило
//   fb_back = 3 - f + 1 = 3 или 4 — буфера с таким номером нет, кадр писался за конец fb_pix
//   прямо в zx_screen и соседей: полосы, отвал клавиатуры, падение (LoadProhibited по 0x100).
//   На 50 Гц фаза VGA постоянна и в это окно не попадала; на 60 Гц плывёт — сбой за 10–30 с.
//   Теперь считаем только по копиям, взятым под замком (pub, f), и проверяем диапазон.
static void fb_publish() {
  int8_t pub = fb_back, f;
  FB_LOCK();
  fb_ready = pub;
  f = fb_front;
  FB_UNLOCK();
  int8_t nb = (f == pub) ? (int8_t)((pub + 1) % 3) : (int8_t)(3 - f - pub);
  if (nb < 0 || nb > 2 || nb == pub) nb = (int8_t)((pub + 1) % 3);   // страховка: номер всегда 0..2
  fb_back = nb;
  fb_on = true;
}

static bool z80_halted = false;  // Claude (01.10.2026): процессор стоит на HALT (ждёт INT/NMI)

static void run_frame() {
  // Прерывание в начале кадра (как у ULA). 0xFF — "плавающая шина" для IM 2.
  int used = Z80Interrupt(&z80, 0xFF, nullptr);
  if (used > 0) z80_halted = false;   // Claude: прерывание принято — HALT закончился
  int target = FRAME_TSTATES - frame_carry - used;
  int done = 0;
  fb_ln = 0;
  // Claude: Цикл, потому что z80emu останавливается на ED xx (перехват ленты) —
  //   обрабатываем и доигрываем кадр до конца.
  // Claude (01.10.2026): и ещё на границах строк — снимок экрана по лучу (fb_capture_upto),
  //   и на HALT: дальше до конца кадра процессор стоит (раньше это делал сам z80emu, но
  //   только до конца одного вызова Z80Emulate).
  while (done < target) {
    fb_capture_upto(used + done);
    int lim = target;                                   // до какого такта гнать этот кусок
    if (fb_ln < FB_LINES && fb_line_t(fb_ln) - used < lim) lim = fb_line_t(fb_ln) - used;
    if (lim <= done) lim = done + 1;
    if (magic_frames > 0) {
      // Claude: MAGIC. По руководству вход в монитор возможен, только когда процессор
      //   выполняет команды из ОЗУ. Пока ждём — идём по одной команде и проверяем PC.
      //   Дождались: включаем DOS/ (ROM Monitor/TR-DOS) и даём NMI -> #0066.
      if ((z80.pc & 0xFFFF) >= 0x4000) {
        zx_dos_set(1);
        zx_tbase = used + done;
        done += Z80NonMaskableInterrupt(&z80, nullptr);
        z80_halted = false;                   // Claude: NMI тоже выводит из HALT
        magic_frames = 0;
        Serial.printf("[magic] NMI, ROM page %u\n", (unsigned)zx_rom_page);
        continue;
      }
      if (z80_halted) { done = lim; continue; }   // Claude: стоим на HALT — такты идут, команд нет
      zx_tbase = used + done;                 // Claude: база времени для событий звука
      done += Z80Emulate(&z80, 1, nullptr);   // одна команда
    } else {
      if (z80_halted) { done = lim; continue; }
      zx_tbase = used + done;
      done += Z80Emulate(&z80, lim - done, nullptr);
    }
    if (z80.status == Z80_STATUS_HALT) {      // Claude: выполнили HALT — PC уже после него (статусы — числа, не биты)
      z80_halted = true;
      if (done < lim) done = lim;
    } else if (z80.status == Z80_STATUS_ED_UNDEFINED) {
      if ((z80.pc & 0xFFFF) == TAPE_TRAP_ADDR && zx_rom_page == 1 && zx_rom0_is_rom) tape_trap();
      else z80.pc = (z80.pc + 2) & 0xFFFF;     // прочие ED xx: на реальном Z80 это NOP
    }
  }
  fb_capture_upto(1 << 30);                    // Claude: на всякий случай — все строки сняты
  fb_publish();
  if (magic_frames > 0 && --magic_frames == 0)
    Serial.println("[magic] no reaction: CPU is running ROM code (as on real Scorpion)");
  frame_carry = done - target;
  inj_step();
  zx_frame = zx_frame + 1;     // Claude: не "++" — для volatile компилятор предупреждает (C++20)
}
// ==== TAPE END ====

// ---------------------------------------------------------------------------
// Claude: ВЫВОД ЗВУКА.
// ---------------------------------------------------------------------------
static volatile uint32_t audio_underruns = 0;
#if AUDIO_OUT == 1
// Claude (01.10): ЧТО: I2S на PCM5102, 48 кГц, стерео, слоты по 32 бита (BCK = 64 fs).
//   ПОЧЕМУ 32-битные слоты: при SCK на земле PCM5102 делает тактовую сам из BCK, и 64 fs —
//   его штатный режим; 16-битные данные кладём в старшие биты.
//   ПОЧЕМУ так с темпом: кадр пишет 960 сэмплов в DMA без ожидания. Эмуляция идёт по
//   esp_timer, I2S — от PLL; оба от одного кварца, поэтому не расходятся. Запас DMA 80 мс.
//   Нет данных (пауза в меню F9) — DMA сам шлёт нули (auto_clear): тишина, а не зацикленный
//   кусок звука.
//   ФИЛЬТР: уровень синтеза всегда >= 0 (как на реальном динамике), а у PCM5102 выход без
//   разделительного конденсатора. Поэтому ставим фильтр от постоянной составляющей (~12 Гц),
//   как разделительный конденсатор в настоящем Спектруме — иначе щелчки при старте звука.
#include "driver/i2s_std.h"
static i2s_chan_handle_t i2s_tx = nullptr;
static volatile uint32_t audio_dropped = 0;     // сэмплов, не влезших в DMA

static bool IRAM_ATTR i2s_on_ovf(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  audio_underruns = audio_underruns + 1;        // DMA дошёл до пустого буфера
  return false;
}

static bool audio_begin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 480;                       // 8 x 10 мс = 80 мс
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &i2s_tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_PIN_BCK,
      .ws   = (gpio_num_t)I2S_PIN_WS,
      .dout = (gpio_num_t)I2S_PIN_DOUT,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_channel_init_std_mode(i2s_tx, &sc) != ESP_OK) return false;
  i2s_event_callbacks_t cb = {};
  cb.on_send_q_ovf = i2s_on_ovf;
  i2s_channel_register_event_callback(i2s_tx, &cb, nullptr);
  static int32_t zero[2 * AUDIO_SPF];           // запас 2 кадра тишины до старта
  size_t n = 0;
  for (int k = 0; k < 2; k++) i2s_channel_preload_data(i2s_tx, zero, sizeof zero, &n);
  return i2s_channel_enable(i2s_tx) == ESP_OK;
}

// Claude: Вызывается после каждого кадра: синтез 960 сэмплов и в I2S.
static void audio_frame_done() {
  static uint16_t L[AUDIO_SPF], R[AUDIO_SPF];
  static int32_t buf[2 * AUDIO_SPF];
  static int32_t xl = 0, yl = 0, xr = 0, yr = 0;  // состояние фильтра (x — вход, y — выход)
  audio_render_frame(L, R);
  for (int i = 0; i < AUDIO_SPF; i++) {
    // уровень 0..542 -> x 0..26016; y = x - x_пред + 0.9985*y_пред (фильтр ~12 Гц)
    int32_t x = L[i] * 48; yl = x - xl + (int32_t)(((int64_t)yl * 65437) >> 16); xl = x;
    x = R[i] * 48;         yr = x - xr + (int32_t)(((int64_t)yr * 65437) >> 16); xr = x;
    int32_t a = yl < -32767 ? -32767 : (yl > 32767 ? 32767 : yl);
    int32_t b = yr < -32767 ? -32767 : (yr > 32767 ? 32767 : yr);
    buf[2 * i] = a << 16;                       // 16 бит в старшей половине 32-битного слота
    buf[2 * i + 1] = b << 16;
  }
  size_t w = 0;
  i2s_channel_write(i2s_tx, buf, sizeof buf, &w, 0);   // без ожидания: темп держит esp_timer
  if (w < sizeof buf) audio_dropped += (uint32_t)((sizeof buf - w) / 8);
}
#else
// Claude: ШИМ (сигма-дельта). Кольцевой буфер сэмплов: кадр кладёт 625, таймер (31250 Гц,
//   прерывание на ядре 1) забирает по одному и выставляет плотность сигма-дельты.
//   Производитель и потребитель — на одном ядре; индексы пишет каждый свой.
//   Старт вывода — когда накопилось 2 кадра (запас на неровность кадров ~40 мс).
#include "driver/sdm.h"
#include "driver/gptimer.h"

static const uint32_t ARB = 4096;             // размер кольца (степень двойки), ~6.5 кадра
static int8_t arbL[ARB], arbR[ARB];
static volatile uint32_t arb_w = 0, arb_r = 0;
static volatile bool     arb_run = false;
static volatile uint32_t audio_dropped = 0;
static sdm_channel_handle_t sdm_l = nullptr, sdm_r = nullptr;

static bool IRAM_ATTR audio_timer_isr(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *) {
  if (!arb_run) return false;
  uint32_t r = arb_r;
  if (r == arb_w) { audio_underruns = audio_underruns + 1; return false; }  // пусто: держим уровень
  uint32_t i = r & (ARB - 1);
  sdm_channel_set_pulse_density(sdm_l, arbL[i]);
  if (sdm_r) sdm_channel_set_pulse_density(sdm_r, arbR[i]);
  arb_r = r + 1;
  return false;
}

static bool audio_begin() {
  sdm_config_t sc = {};
  sc.clk_src = SDM_CLK_SRC_DEFAULT;
  sc.sample_rate_hz = 10 * 1000 * 1000;       // Claude: несущая 10 МГц (80 МГц / 8) — фильтру легко
  sc.gpio_num = AUDIO_PIN_L;
  if (sdm_new_channel(&sc, &sdm_l) != ESP_OK) return false;
  sdm_channel_enable(sdm_l);
  sdm_channel_set_pulse_density(sdm_l, -128);
#if AUDIO_MODE != 0
  sc.gpio_num = AUDIO_PIN_R;
  if (sdm_new_channel(&sc, &sdm_r) != ESP_OK) return false;
  sdm_channel_enable(sdm_r);
  sdm_channel_set_pulse_density(sdm_r, -128);
#endif
  gptimer_handle_t tm = nullptr;
  gptimer_config_t tc = {};
  tc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  tc.direction = GPTIMER_COUNT_UP;
  tc.resolution_hz = 1000000;                 // 1 МГц
  if (gptimer_new_timer(&tc, &tm) != ESP_OK) return false;
  gptimer_event_callbacks_t cb = {};
  cb.on_alarm = audio_timer_isr;
  gptimer_register_event_callbacks(tm, &cb, nullptr);
  gptimer_alarm_config_t ac = {};
  ac.alarm_count = 1000000 / AUDIO_RATE;      // 32 мкс = 31250 Гц ровно
  ac.reload_count = 0;
  ac.flags.auto_reload_on_alarm = 1;
  gptimer_set_alarm_action(tm, &ac);
  gptimer_enable(tm);
  gptimer_start(tm);
  return true;
}

// Claude: Вызывается после каждого кадра: синтез 625 сэмплов и в кольцо.
static void audio_frame_done() {
  static uint16_t L[AUDIO_SPF], R[AUDIO_SPF];
  audio_render_frame(L, R);
  uint32_t w = arb_w, used = w - arb_r;
  if (used + AUDIO_SPF > ARB) { audio_dropped += AUDIO_SPF; return; }   // переполнено — кадр пропускаем
  for (int i = 0; i < AUDIO_SPF; i++) {       // уровень 0..542 -> плотность -128..127
    arbL[(w + i) & (ARB - 1)] = (int8_t)((L[i] * 481 >> 10) - 128);
    arbR[(w + i) & (ARB - 1)] = (int8_t)((R[i] * 481 >> 10) - 128);
  }
  arb_w = w + AUDIO_SPF;
  if (!arb_run && (arb_w - arb_r) >= (uint32_t)(2 * AUDIO_SPF)) arb_run = true;
}
#endif

// Claude: Грубый ASCII-скриншот в Serial (как был у Арча) — для проверки без монитора.
static void print_ascii_screen() {
  Serial.println("\n=== SCREENSHOT (32x24, # = есть точки) ===");
  for (int row = 0; row < 24; row++) {
    int base = 0x4000 + (row / 8) * 2048 + (row % 8) * 32;
    for (int col = 0; col < 32; col++) {
      bool ink = false;
      for (int sl = 0; sl < 8; sl++) if (zx_screen[base - 0x4000 + sl * 256 + col]) { ink = true; break; }
      Serial.print(ink ? '#' : ' ');
    }
    Serial.println();
  }
  Serial.println("=== END ===\n");
}

// ==== STORAGE BEGIN (Claude: образы дисков во flash (FAT) и режим USB-диска; только ESP32) ====
// ---------------------------------------------------------------------------
// Claude: ОБРАЗЫ ДИСКОВ ВО FLASH.
//   Раздел "ffat" (Tools -> Partition Scheme -> 16M Flash (2MB APP/12.5MB FATFS),
//   Flash Size 16MB). Файлы .trd/.scl в корне.
//
//   ВАЖНО (найдено 24.09): на плате flash Spansion/Infineon S25FL127S/128S (ID 01 2018).
//   Команда стирания 4 КБ у неё работает только на служебных секторах в начале/конце
//   адресного пространства, остальное — секторы 64 КБ (4-КБ стирание молча игнорируется).
//   Поэтому wear levelling ядра (стирает по 4 КБ) и FFat здесь не работают, а прошивка из
//   IDE проходит только с "Erase All Flash". Раздел ведём сами, блоками по 64 КБ:
//   ПК видит диск с секторами 4096 байт; запись копится в кэше одного 64-КБ блока (PSRAM),
//   блок стирается одной 64-КБ командой и пишется целиком. Эмулятор читает FAT
//   только на чтение (esp_vfs_fat_spiflash_mount_ro — без wear levelling).
//   Форматировать с ПК: sudo mkfs.vfat -I -S 4096 -n ZXDISKS /dev/sdX
//
//   Разъём USB при старте (разъём COM не участвует):
//   1) ESP32 включается USB-флешкой (MSC) и ждёт опроса USB_DETECT_MS;
//   2) ПК опросил -> режим диска: SD-карта, если вставлена (Claude 01.10), иначе эта flash;
//   3) тишина -> флажок в RTC-памяти, перезагрузка
//      в USB-хост (клавиатура) — обычный Спектрум.
// ---------------------------------------------------------------------------
static RTC_NOINIT_ATTR uint32_t boot_to_host;             // переживает esp_restart()
static const uint32_t BOOT_HOST_MAGIC = 0x5A534831u;
static const uint32_t USB_DETECT_MS = 1500;               // сколько ждать опроса от ПК
static const uint32_t MSC_SEC = 4096;                     // сектор, который видит ПК
static const uint32_t MSC_BLK = 65536;                    // блок стирания flash
static USBMSC usb_msc;
static const esp_partition_t *msc_part = nullptr;
static uint8_t  *msc_cache = nullptr;                     // кэш одного 64-КБ блока (PSRAM)
static uint8_t  *msc_vbuf = nullptr;                      // буфер проверки (PSRAM)
static int32_t   msc_cache_blk = -1;
static bool      msc_dirty = false;
static uint32_t  msc_last_wr_ms = 0;
static SemaphoreHandle_t msc_mux = nullptr;
static volatile bool msc_ejected = false;
static volatile uint32_t msc_rd_kb = 0, msc_wr_kb = 0, msc_err = 0, msc_verr = 0, msc_flushes = 0;
static int msc_fat_state = 0;                              // 1 = загрузочный сектор FAT на месте

// Claude: записать кэшированный блок: стереть 64 КБ одной командой, записать, проверить.
//   Вызывать под msc_mux.
static void msc_flush_locked() {
  if (!msc_dirty || msc_cache_blk < 0) return;
  uint32_t off = (uint32_t)msc_cache_blk * MSC_BLK;
  bool ok = esp_partition_erase_range(msc_part, off, MSC_BLK) == ESP_OK &&
            esp_partition_write(msc_part, off, msc_cache, MSC_BLK) == ESP_OK;
  if (!ok) msc_err++;
  else if (msc_vbuf && (esp_partition_read(msc_part, off, msc_vbuf, MSC_BLK) != ESP_OK ||
                        memcmp(msc_vbuf, msc_cache, MSC_BLK))) msc_verr++;
  msc_dirty = false;
  msc_flushes++;
}

// Claude (01.10): SD-карта — общая часть (нужна и режиму USB-диска, и эмулятору).
static const int SD_PIN_CS = 10, SD_PIN_MOSI = 11, SD_PIN_CLK = 12, SD_PIN_MISO = 13;
static SPIClass sd_spi(FSPI);
static bool sd_ok = false;
static SemaphoreHandle_t sd_mux = nullptr;              // образ в памяти + файл на SD

static void storage_init_sd() {
  static bool done = false;                             // Claude (01.10): зовётся и из режима USB-диска
  if (done) return;
  done = true;
  sd_mux = xSemaphoreCreateMutex();
  sd_spi.begin(SD_PIN_CLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
  sd_ok = SD.begin(SD_PIN_CS, sd_spi, 20000000, "/sd", 4) ||
          SD.begin(SD_PIN_CS, sd_spi, 4000000, "/sd", 4);   // длинные провода — вторая попытка медленнее
  if (sd_ok) Serial.printf("SD: card %u MB\n", (unsigned)(SD.cardSize() >> 20));
  else       Serial.println("SD: no card");
}

static int32_t msc_read(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  uint32_t addr = lba * MSC_SEC + offset;
  int32_t r = (int32_t)size;
  xSemaphoreTake(msc_mux, portMAX_DELAY);
  if ((int32_t)(addr / MSC_BLK) == msc_cache_blk && (addr % MSC_BLK) + size <= MSC_BLK)
    memcpy(buf, msc_cache + addr % MSC_BLK, size);
  else if (esp_partition_read(msc_part, addr, buf, size) != ESP_OK) { msc_err++; r = -1; }
  xSemaphoreGive(msc_mux);
  msc_rd_kb += size / 1024;
  return r;
}

static int32_t msc_write(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  uint32_t addr = lba * MSC_SEC + offset;
  int32_t blk = (int32_t)(addr / MSC_BLK);
  if ((addr % MSC_BLK) + size > MSC_BLK) { msc_err++; return -1; }   // сектор не пересекает блок
  xSemaphoreTake(msc_mux, portMAX_DELAY);
  if (blk != msc_cache_blk) {
    msc_flush_locked();
    if (esp_partition_read(msc_part, (uint32_t)blk * MSC_BLK, msc_cache, MSC_BLK) != ESP_OK) {
      msc_err++; msc_cache_blk = -1;
      xSemaphoreGive(msc_mux);
      return -1;
    }
    msc_cache_blk = blk;
  }
  memcpy(msc_cache + addr % MSC_BLK, buf, size);
  msc_dirty = true;
  msc_last_wr_ms = millis();
  xSemaphoreGive(msc_mux);
  msc_wr_kb += size / 1024;
  return (int32_t)size;
}

// Claude (01.10): ЧТО: режим USB-диска для SD-карты — ПК видит карту целиком, как в картридере
//   (512-байтные сектора, её разметка и FAT32). ПОЧЕМУ напрямую секторами (readRAW/writeRAW):
//   файловую систему ведёт ПК, эмулятор в этом режиме не работает — конфликтов нет.
//   TinyUSB отдаёт кусками до 4 КБ (CONFIG_TINYUSB_MSC_BUFSIZE), т.е. по нескольку секторов.
static bool msc_on_sd = false;
static int32_t msc_sd_read(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  if (offset % 512 || size % 512) { msc_err++; return -1; }
  for (uint32_t i = 0; i < size / 512; i++)
    if (!SD.readRAW((uint8_t *)buf + i * 512, lba + offset / 512 + i)) { msc_err++; return -1; }
  msc_rd_kb += size / 1024;
  return (int32_t)size;
}
static int32_t msc_sd_write(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  if (offset % 512 || size % 512) { msc_err++; return -1; }
  for (uint32_t i = 0; i < size / 512; i++)
    if (!SD.writeRAW(buf + i * 512, lba + offset / 512 + i)) { msc_err++; return -1; }
  msc_wr_kb += size / 1024;
  return (int32_t)size;
}

static bool msc_start_stop(uint8_t power, bool start, bool load_eject) {
  if (load_eject && !start) {                             // ПК: "Извлечь"
    if (!msc_on_sd) {                                     // flash: дописать кэш блока
      xSemaphoreTake(msc_mux, portMAX_DELAY);
      msc_flush_locked();
      xSemaphoreGive(msc_mux);
    }                                                     // SD пишется сразу, кэша нет
    msc_ejected = true;
  }
  return true;
}

// Claude: самотест 64-КБ стирания на последнем блоке раздела (содержимое сохраняется
//   и возвращается). 0 = OK. Проверяет, что ESP-IDF стирает выровненные 64 КБ блоком.
static int flash_selftest() {
  if (!msc_cache || !msc_vbuf) return 1;
  uint32_t off = msc_part->size - MSC_BLK;
  if (esp_partition_read(msc_part, off, msc_cache, MSC_BLK) != ESP_OK) return 2;   // сохранить
  for (uint32_t i = 0; i < MSC_BLK; i++) msc_vbuf[i] = (uint8_t)(i * 7 + 13);
  int res = 0;
  if (esp_partition_erase_range(msc_part, off, MSC_BLK) != ESP_OK ||
      esp_partition_write(msc_part, off, msc_vbuf, MSC_BLK) != ESP_OK) res = 3;
  if (!res && esp_partition_erase_range(msc_part, off, MSC_BLK) != ESP_OK) res = 4;  // стереть снова
  if (!res) {                                             // после стирания — всё FF?
    if (esp_partition_read(msc_part, off, msc_vbuf, MSC_BLK) != ESP_OK) res = 5;
    else for (uint32_t i = 0; i < MSC_BLK; i++) if (msc_vbuf[i] != 0xFF) { res = 6; break; }
  }
  esp_partition_erase_range(msc_part, off, MSC_BLK);      // вернуть содержимое
  esp_partition_write(msc_part, off, msc_cache, MSC_BLK);
  return res;
}

// Claude: Текст на экран Спектрума (ОЗУ 5), шрифт из ROM Basic 48 (#3D00).
static uint8_t *scr_buf = nullptr;                       // куда рисуем (nullptr = ОЗУ 5)
static inline uint8_t *scr_mem() { return scr_buf ? scr_buf : zx_ram[5]; }
static void scr_clear(uint8_t attr) {
  memset(scr_mem(), 0, 6144);
  memset(scr_mem() + 6144, attr, 768);
}
static void scr_print(int row, int col, const char *t) {
  const uint8_t *font = rom_scorpion + 0x4000 + 0x3D00;
  for (; *t && col < 32; t++, col++) {
    uint8_t c = (uint8_t)*t;
    if (c < 32 || c > 127) c = '?';
    for (int k = 0; k < 8; k++)
      scr_mem()[((row & 0x18) << 8) | (k << 8) | ((row & 7) << 5) | col] = font[(c - 32) * 8 + k];
  }
}
static void scr_attr_row(int row, uint8_t attr) { memset(scr_mem() + 6144 + row * 32, attr, 32); }
static void scr_clear_row(int row) {
  for (int k = 0; k < 8; k++) memset(scr_mem() + (((row & 0x18) << 8) | (k << 8) | ((row & 7) << 5)), 0, 32);
}

static const esp_partition_t *ffat_partition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat");
}

// Claude: Проверка при старте. true — ПК опросил, мы в режиме диска.
//   false — ПК нет или раздела нет: работаем Спектрумом.
static bool usb_disk_mode_try() {
  if (boot_to_host == BOOT_HOST_MAGIC) {                  // уже проверяли — это клавиатура
    boot_to_host = 0;
    return false;
  }
  usb_msc.vendorID("Scorpion");
  usb_msc.productRevision("1.0");
  usb_msc.onStartStop(msc_start_stop);
  usb_msc.mediaPresent(true);
  usb_msc.isWritable(true);
  storage_init_sd();                                      // Claude (01.10): карта есть — отдаём её
  if (sd_ok) {
    msc_on_sd = true;
    usb_msc.productID("ZS-256 SD card");
    usb_msc.onRead(msc_sd_read);
    usb_msc.onWrite(msc_sd_write);
    usb_msc.begin((uint32_t)SD.numSectors(), 512);
  } else {
  msc_part = ffat_partition();
  if (!msc_part) {
    Serial.println("No 'ffat' partition. Tools -> Partition Scheme -> 16M Flash (2MB APP/12.5MB FATFS)");
    return false;
  }
  msc_cache = (uint8_t *)heap_caps_malloc(MSC_BLK, MALLOC_CAP_SPIRAM);
  msc_vbuf  = (uint8_t *)heap_caps_malloc(MSC_BLK, MALLOC_CAP_SPIRAM);
  msc_mux = xSemaphoreCreateMutex();
  if (!msc_cache || !msc_vbuf || !msc_mux) { Serial.println("MSC: no memory"); return false; }
  uint8_t bs[512];                                        // узнаваема ли FAT
  msc_fat_state = (esp_partition_read(msc_part, 0, bs, sizeof bs) == ESP_OK &&
                   bs[510] == 0x55 && bs[511] == 0xAA &&
                   (!memcmp(bs + 54, "FAT", 3) || !memcmp(bs + 82, "FAT", 3))) ? 1 : 2;
  usb_msc.productID("ZS-256 disks");
  usb_msc.onRead(msc_read);
  usb_msc.onWrite(msc_write);
  usb_msc.begin(msc_part->size / MSC_SEC, (uint16_t)MSC_SEC);
  }
  USB.begin();
  uint32_t t0 = millis();
  while (millis() - t0 < USB_DETECT_MS) {
    if (USB) {                                            // ПК настроил устройство
      if (msc_on_sd) Serial.printf("USB DISK MODE: SD card, %u MB\n", (unsigned)(SD.cardSize() >> 20));
      else           Serial.printf("USB DISK MODE: flash, %u KB\n", (unsigned)(msc_part->size / 1024));
      return true;
    }
    delay(10);
  }
  Serial.println("No PC on USB -> restart as USB host (keyboard)");
  Serial.flush();
  boot_to_host = BOOT_HOST_MAGIC;
  esp_restart();
  return false;
}

// Claude: Режим диска: картинка на экране, запись кэша через 0.5 с после последней записи.
static void usb_disk_mode_run() {
  fb_on = false;                     // Claude (01.10.2026): экран режима рисуется прямо в память — VGA показывает её
  zx_border = 1;
  scr_clear(0x0F);                                        // белые буквы на синем
  scr_print(2, 5, "SCORPION ZS-256 DISKS");
  scr_print(5, 2, "USB DISK MODE");
  scr_print(7, 2, "Copy .TRD / .SCL files");
  scr_print(8, 2, "to the disk on your PC.");
  scr_print(10, 2, "Then eject it and unplug.");
  scr_print(11, 2, "Spectrum: power via COM,");
  scr_print(12, 2, "keyboard into USB.");
  char b[40];
  if (msc_on_sd) {                                        // Claude (01.10)
    snprintf(b, sizeof b, "SD card, %u MB", (unsigned)(SD.cardSize() >> 20));
    scr_print(14, 2, b);
  } else {
  int st = flash_selftest();
  Serial.printf("Flash 64K erase selftest: %d\n", st);
  snprintf(b, sizeof b, "FAT: %s  Flash test %s%d", msc_fat_state == 1 ? "OK" : "none",
           st ? "FAIL" : "OK", st);
  scr_print(14, 2, b);
  if (msc_fat_state != 1) {
    scr_print(15, 2, "Format on PC:");
    scr_print(16, 2, "mkfs.vfat -I -S 4096 /dev/sdX");
  }
  }
  bool shown = false;
  uint32_t tick = 0;
  while (true) {
    if (!msc_on_sd) {                                     // Claude (01.10): кэш блока — только у flash
      xSemaphoreTake(msc_mux, portMAX_DELAY);
      if (msc_dirty && millis() - msc_last_wr_ms > 500) msc_flush_locked();
      xSemaphoreGive(msc_mux);
    }
    if (++tick % 5 == 0) {                                // обмен с ПК — на экран
      snprintf(b, sizeof b, "Read %6u KB  Write %6u KB", (unsigned)msc_rd_kb, (unsigned)msc_wr_kb);
      scr_print(19, 1, b);
      snprintf(b, sizeof b, "Blocks %u  Err %u  Verify %u   ", (unsigned)msc_flushes, (unsigned)msc_err, (unsigned)msc_verr);
      scr_print(20, 1, b);
      if (tick % 50 == 0) Serial.printf("MSC: read %u KB, write %u KB, blocks %u, err %u, verify %u\n",
                                        (unsigned)msc_rd_kb, (unsigned)msc_wr_kb, (unsigned)msc_flushes,
                                        (unsigned)msc_err, (unsigned)msc_verr);
    }
    if (msc_ejected && !shown) {
      scr_print(22, 2, "Ejected - you can unplug now.");
      Serial.println("USB disk ejected");
      shown = true;
    }
    delay(100);
  }
}

// Claude: Список образов .trd/.scl из корня FAT (по алфавиту) и вставка образа в дисковод.
//   FAT монтируется только на чтение, без wear levelling, и сразу отмонтируется.
static const int DRIVES_USED = 2;                        // у Скорпиона два дисковода: A и B
// Claude (01.10): было 128 во внутренней RAM — 180 дискет Майка не влезали. Теперь 1024
//   имени в PSRAM (64 КБ), выделяется при первом чтении каталога.
static const int IMG_MAX = 1024;
static char (*img_names)[64] = nullptr;
static int  img_count = 0;
static char drv_name[DRIVES_USED][64];                   // что вставлено (для меню)

// Claude (01.10): SD-КАРТА. ЧТО: образы берутся с SD (корень), если карта есть; иначе — из
//   flash, как раньше. На SD .trd пишутся обратно: изменённые дорожки (по 4 КБ) сохраняет
//   задача на ядре 0 через ~0.5 с после последней записи TR-DOS. .scl — только чтение
//   (формат не позволяет дописывать), изменения в них живут до выключения.
//   ПОЧЕМУ задача на ядре 0: запись дорожки на карту — десятки мс, в loop() она ломала бы
//   темп кадров. ПОЧЕМУ SD не мешает VGA: SPI-карта не отключает кэш flash (в отличие от
//   внутренней flash), поэтому видеопрерывание не ждёт.
//   Пины FSPI (сверено по даташиту S3 и pins_arduino.h ядра): CS 10, MOSI 11, CLK 12, MISO 13.
//   Карта — FAT32 (FAT16), до 32 ГБ.
static const char *img_base = "/ffat";                  // откуда список: "/sd" или "/ffat"
static char drv_path[DRIVES_USED][80];                  // полный путь вставленного образа
static bool drv_wr[DRIVES_USED];                        // можно сохранять на SD (.trd с карты)

static bool fat_mount() {                                // Claude (01.10): источник образов
  if (sd_ok) { img_base = "/sd"; return true; }          // SD смонтирована всё время
  img_base = "/ffat";
  if (!ffat_partition()) return false;
  esp_vfs_fat_mount_config_t conf = {};
  conf.max_files = 2;
  esp_err_t e = esp_vfs_fat_spiflash_mount_ro("/ffat", "ffat", &conf);
  if (e != ESP_OK) { Serial.printf("FAT (ro) mount failed: %s\n", esp_err_to_name(e)); return false; }
  return true;
}
static void fat_unmount() { if (!sd_ok) esp_vfs_fat_spiflash_unmount_ro("/ffat", "ffat"); }

// Claude (01.10): Сохранить изменённые дорожки дисковода d в его .trd на SD.
//   Флажок дорожки снимается ДО копирования: если TR-DOS допишет её в это время,
//   флажок встанет снова и дорожка сохранится в следующий раз.
static void storage_flush_drive(int d) {
  if (d < 0 || d >= DRIVES_USED || !drv_wr[d]) return;
  static uint8_t tbuf[FDD_SECS * FDD_SECSZ];            // одна дорожка, 4 КБ
  xSemaphoreTake(sd_mux, portMAX_DELAY);
  FILE *f = nullptr;
  int saved = 0;
  for (int tr = 0; tr < FDD_CYLS * 2 && fdd_img[d]; tr++) {
    uint32_t m = 1u << (tr & 31);
    if (!(__atomic_fetch_and(&fdd_tdirty[d][tr >> 5], ~m, __ATOMIC_RELAXED) & m)) continue;
    memcpy(tbuf, fdd_img[d] + (uint32_t)tr * sizeof tbuf, sizeof tbuf);
    if (!f) f = fopen(drv_path[d], "r+b");
    if (!f) { Serial.printf("SD: can't write %s\n", drv_path[d]); drv_wr[d] = false; break; }
    fseek(f, (long)tr * sizeof tbuf, SEEK_SET);          // за концом короткого .trd файл дорастёт
    if (fwrite(tbuf, 1, sizeof tbuf, f) != sizeof tbuf) Serial.printf("SD: write error %s\n", drv_path[d]);
    saved++;
  }
  if (f) fclose(f);
  xSemaphoreGive(sd_mux);
  if (saved) Serial.printf("SD: %c: saved %d track(s)\n", 'A' + d, saved);
}

static bool drive_has_dirty(int d) {
  for (int i = 0; i < 5; i++) if (fdd_tdirty[d][i]) return true;
  return false;
}

static void storage_flush_task(void *) {                // ядро 0, низкий приоритет
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int d = 0; d < DRIVES_USED; d++)
      if (drv_wr[d] && drive_has_dirty(d) && fdc_mode == 0 &&
          (uint32_t)(zx_frame - fdd_wr_frame[d]) > 25)   // 0.5 с тишины от TR-DOS
        storage_flush_drive(d);
  }
}

static void storage_eject(int d) {                       // Claude (01.10): сохранить и вынуть
  storage_flush_drive(d);
  if (sd_mux) xSemaphoreTake(sd_mux, portMAX_DELAY);
  fdd_eject(d);
  drv_name[d][0] = 0; drv_path[d][0] = 0; drv_wr[d] = false;
  if (sd_mux) xSemaphoreGive(sd_mux);
}

static int img_cmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static void storage_scan() {                             // FAT уже смонтирован
  img_count = 0;
  if (!img_names) img_names = (char (*)[64])heap_caps_malloc(IMG_MAX * 64, MALLOC_CAP_SPIRAM);
  if (!img_names) return;
  DIR *dir = opendir(img_base);
  if (!dir) return;
  for (struct dirent *de; (de = readdir(dir)) && img_count < IMG_MAX;) {
    if (de->d_type == DT_DIR) continue;
    size_t l = strlen(de->d_name);
    if (l < 5 || l >= sizeof img_names[0]) continue;
    const char *ext = de->d_name + l - 4;
    if (strcasecmp(ext, ".trd") && strcasecmp(ext, ".scl")) continue;
    strcpy(img_names[img_count++], de->d_name);
  }
  closedir(dir);
  qsort(img_names, img_count, sizeof img_names[0], img_cmp);   // Claude (01.10): по алфавиту
}

static bool storage_insert(int d, const char *name) {    // FAT уже смонтирован
  char path[80];
  snprintf(path, sizeof path, "%s/%s", img_base, name);
  storage_flush_drive(d);                                // Claude (01.10): старый диск — сохранить
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = (len > 0 && len <= 700000) ? (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : nullptr;
  bool ok = buf && fread(buf, 1, len, f) == (size_t)len;
  fclose(f);
  if (ok) {                                              // Claude (01.10): замена образа — под sd_mux
    if (sd_mux) xSemaphoreTake(sd_mux, portMAX_DELAY);
    ok = fdd_insert(d, buf, (uint32_t)len, false);
    drv_wr[d] = ok && sd_ok && strcasecmp(name + strlen(name) - 4, ".trd") == 0;
    if (ok) { strncpy(drv_name[d], name, sizeof drv_name[d] - 1); strncpy(drv_path[d], path, sizeof drv_path[d] - 1); }
    if (sd_mux) xSemaphoreGive(sd_mux);
  }
  if (buf) free(buf);
  Serial.printf("Disk %c: %s %s\n", 'A' + d, name, ok ? "inserted" : "ERROR");
  return ok;
}

// Claude: При старте: первые два образа по алфавиту -> A и B. Возвращает число вставленных.
static int storage_load_disks() {
  if (!fat_mount()) return 0;
  storage_scan();
  Serial.printf("%s: %d disk image(s)\n", sd_ok ? "SD" : "FAT", img_count);
  for (int i = 0; i < img_count; i++) Serial.printf("  %s\n", img_names[i]);
  int ins = 0;
  for (int i = 0; i < img_count && ins < DRIVES_USED; i++)
    if (storage_insert(ins, img_names[i])) ins++;
  fat_unmount();
  return ins;
}

// Claude: МЕНЮ ДИСКОВ (F9). Эмуляция на паузе, меню рисуется поверх текущего экрана
//   Спектрума (он сохраняется и потом возвращается). Клавиши:
//   Вверх/Вниз, PgUp/PgDn — выбор; A или Enter — в дисковод A; B — в дисковод B;
//   пункт "<empty>" вынимает диск; Esc или F9 — выход.
static void disk_menu_run() {
  static uint8_t saved[6912];
  uint8_t *scr = zx_screen;
  uint8_t saved_border = zx_border;
  memcpy(saved, scr, 6912);
  scr_buf = scr;
  menu_key = 0;
  menu_active = true;
  fb_on = false;                     // Claude (01.10.2026): меню рисуется прямо в экран — VGA показывает живую память;
                                     //   после меню первый же кадр Z80 снова включит буферы (fb_publish)

  bool have_fat = fat_mount();
  if (have_fat) storage_scan(); else img_count = 0;
  const int LIST_TOP = 4, LIST_ROWS = 17;                 // строки 4..20
  int sel = 0, top = 0;                                   // 0 = "<empty>", 1.. = файлы
  const char *msg = nullptr;
  zx_border = 1;
  bool redraw = true, quit = false;
  // Claude (01.10): автоповтор стрелок/PgUp/PgDn: первый шаг — по нажатию, через 0.4 с —
  //   повтор каждые 70 мс, после 2 с удержания — каждые 25 мс (быстрая прокрутка).
  uint8_t hold_prev = 0;
  uint32_t hold_t0 = 0, rep_t = 0;
  while (!quit) {
    if (hard_request) break;                              // Claude (01.10.2026): Ctrl+Alt+End — выйти, перезапуск в loop()
    if (redraw) {
      scr_clear(0x0F);                                    // белое на синем
      scr_attr_row(0, 0x30);
      scr_print(0, 0, sd_ok ? " SCORPION DISKS       SD     F9 " : " SCORPION DISKS       FLASH  F9 ");
      char b[40];
      for (int d = 0; d < DRIVES_USED; d++) {
        snprintf(b, sizeof b, "%c: %.29s", 'A' + d, fdd_img[d] ? (drv_name[d][0] ? drv_name[d] : "(disk)") : "<empty>");
        scr_print(1 + d, 1, b);
      }
      if (!have_fat) scr_print(LIST_TOP, 1, "No SD card, no FAT on flash");
      int total = img_count + 1;
      if (have_fat && sel > 0) {                          // Claude (01.10): позиция в длинном списке
        snprintf(b, sizeof b, "%d / %d", sel, img_count);
        scr_print(3, 31 - (int)strlen(b), b);
      }
      if (sel < top) top = sel;
      if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
      for (int r = 0; r < LIST_ROWS && top + r < total && have_fat; r++) {
        int i = top + r;
        snprintf(b, sizeof b, " %.30s", i == 0 ? "<empty>" : img_names[i - 1]);
        scr_print(LIST_TOP + r, 0, b);
        if (i == sel) scr_attr_row(LIST_TOP + r, 0x28);  // выделение: чёрное на зелёном
      }
      if (msg) { scr_attr_row(21, 0x0E); scr_print(21, 1, msg); }
      scr_attr_row(22, 0x07); scr_attr_row(23, 0x07);
      scr_print(22, 0, " Up/Dn select  A,Enter: drive A");
      scr_print(23, 0, " B: drive B   Esc: back");
      redraw = false;
    }
    keyboard_poll();                                      // USB: клавиши приходят в menu_key
    uint8_t k = menu_key;
    menu_key = 0;
    uint8_t h = menu_hold;                                // Claude (01.10): автоповтор, см. выше
    uint32_t now = millis();
    if (h != hold_prev) { hold_prev = h; hold_t0 = now; rep_t = now; }
    else if (h && now - hold_t0 > 400 && now - rep_t >= (now - hold_t0 > 2000 ? 25u : 70u)) {
      rep_t = now;
      if (!k) k = h;
    }
    int total = img_count + 1;
    switch (k) {
      case 0: break;
      case 0x52: if (sel > 0) sel--; redraw = true; break;                          // Вверх
      case 0x51: if (sel < total - 1) sel++; redraw = true; break;                  // Вниз
      case 0x4B: sel = sel > LIST_ROWS ? sel - LIST_ROWS : 0; redraw = true; break; // PgUp
      case 0x4E: sel = sel + LIST_ROWS < total ? sel + LIST_ROWS : total - 1; redraw = true; break;
      case 0x4A: sel = 0; redraw = true; break;                                     // Claude (01.10): Home
      case 0x4D: sel = total - 1; redraw = true; break;                             //                End
      case 0x04: case 0x28: case 0x58: case 0x05: {       // A, Enter, Enter цифр., B
        if (!have_fat) break;
        int d = (k == 0x05) ? 1 : 0;
        if (sel == 0) { storage_eject(d); msg = "Disk ejected"; }
        else msg = storage_insert(d, img_names[sel - 1]) ? "Disk inserted" : "ERROR reading image";
        redraw = true;
        break;
      }
      case 0x29: case 0x42: quit = true; break;           // Esc, F9
      default: break;
    }
    delay(20);
  }
  if (have_fat) fat_unmount();
  menu_active = false;
  memcpy(scr, saved, 6912);                               // вернуть экран Спектрума
  zx_border = saved_border;
  scr_buf = nullptr;
}
// ==== STORAGE END ====

void setup() {
  loop_task = xTaskGetCurrentTaskHandle();   // Claude (01.10.2026): setup() и loop() — одна задача; её будит on_vsync
  Serial.begin(115200);
  delay(500);
  Serial.println("=== Scorpion ZS-256 + VGA (ESP32-S3) ===");

  // Claude: 12 страниц ОЗУ (192 КБ) — в PSRAM, 4 страницы — во внутренней RAM.
  uint8_t *psram_pages = (uint8_t *)heap_caps_malloc(12 * 0x4000, MALLOC_CAP_SPIRAM);
  if (!psram_pages) {
    Serial.println("ERR: no PSRAM for RAM pages. Tools -> PSRAM -> OPI PSRAM");
    while (true) delay(1000);
  }
  zx_mem_init(psram_pages);
  tape_install_trap();               // Claude: перехват загрузки с ленты (в копии ROM Basic 48)
  zx_machine_reset();                // Claude: порты в 0 -> ROM 0 (меню Скорпиона)
  fdc_reset();                       // Claude: ВГ93

  build_tables();

  xTaskCreatePinnedToCore(video_init_task, "vga_init", 4096, nullptr, 5, nullptr, 0);
  while (video_status == 0) delay(1);
  if (video_status < 0) {
    Serial.printf("ERR VGA init: %s\n", esp_err_to_name(video_err));
    while (true) delay(1000);
  }
  Serial.println("VGA running (core 0).");

  // Claude: разъём USB: ПК -> режим диска (отсюда не выходим), иначе — Спектрум
  if (usb_disk_mode_try()) usb_disk_mode_run();
  storage_init_sd();                 // Claude (01.10): SD есть — образы с неё, иначе из flash
  if (sd_ok) xTaskCreatePinnedToCore(storage_flush_task, "sdflush", 4096, nullptr, 1, nullptr, 0);
  if (storage_load_disks() == 0) {
#ifdef DISK_COUNT
    for (int i = 0; i < DISK_COUNT && i < DRIVES_USED; i++)  // Claude: нет образов во flash — диски из disks.h (A, B)
      Serial.printf("Disk %c: %s %s\n", 'A' + i, disk_name[i],
                    fdd_insert(i, disk_data[i], disk_len[i], false) ? "inserted" : "ERROR"),
      strncpy(drv_name[i], disk_name[i], sizeof drv_name[i] - 1);
#else
    Serial.println("No disk images - drives are empty");
#endif
  }

  Serial.println("Z80 running (core 1). Expect Scorpion menu. F9=disks F10/Ctrl+Alt+Del=Reset F11=Magic F12=tape LOAD \"\"");

  keyboard_begin();                  // Claude: этап 3 — USB-клавиатура

  ay_reset();
#if AUDIO_OUT == 1
  if (audio_begin()) Serial.printf("Audio: I2S PCM5102, %d Hz, mode %d, BCK %d, LCK %d, DIN %d\n",   // Claude (01.10)
                                   AUDIO_RATE, AUDIO_MODE, I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DOUT);
#else
  if (audio_begin()) Serial.printf("Audio: sigma-delta, %d Hz, mode %d, pins %d/%d\n",
                                   AUDIO_RATE, AUDIO_MODE, AUDIO_PIN_L, AUDIO_PIN_R);
#endif
  else               Serial.println("ERR: audio init failed");
  // Claude (01.10.2026): контроль: буферы кадра "по лучу" заняли ~37 КБ внутренней RAM.
  Serial.printf("[mem] internal RAM free %u, largest block %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// Claude (01.10.2026): ПОЛНЫЙ ПЕРЕЗАПУСК ESP32 (Ctrl+Alt+End).
//   ЧТО: сначала дописать на SD изменённые дорожки .trd (иначе последняя запись TR-DOS
//   пропадёт), потом esp_restart() — то же, что кнопка сброса на плате.
//   ПОЧЕМУ флажок boot_to_host: раз клавиатура нажата, ПК на разъёме USB точно нет — сразу
//   стартуем USB-хостом, без 1.5 с ожидания и лишней перезагрузки.
//   После перезапуска в A и B снова первые два образа по алфавиту (как при включении).
static void hard_restart() {
  Serial.println("[hard reset] Ctrl+Alt+End: saving disks, restarting ESP32");
  if (sd_ok) for (int d = 0; d < DRIVES_USED; d++) storage_flush_drive(d);
  Serial.flush();
  boot_to_host = BOOT_HOST_MAGIC;
  esp_restart();
}

void loop() {
  static int64_t next_us = 0;
  static int64_t emu_us_acc = 0;
  static uint32_t frames_in_sec = 0, late = 0;
  static uint32_t last_print_ms = 0;
  static uint32_t last_frames_sysvar = 0;
  static bool shot_done = false;

  if (next_us == 0) next_us = esp_timer_get_time();

  // --- Один кадр Спектрума ---
  int64_t t0 = esp_timer_get_time();
  run_frame();                       // Claude: кадр Спектрума (+ перехват ленты, автонабор)
  audio_frame_done();                // Claude: синтез звука этого кадра
  emu_us_acc += esp_timer_get_time() - t0;
  frames_in_sec++;

  keyboard_poll();                   // Claude: нажатия попадают в матрицу между кадрами (раз в 20 мс)

  // Claude: F12 или автозагрузка при старте: перемотать ленту и набрать LOAD "".
  //   Набор имеет смысл в режиме K (сразу после старта или после сообщения "OK").
  if (hard_request) hard_restart();  // Claude (01.10.2026): Ctrl+Alt+End — перезапуск ESP32
  if (f10_request) {                 // Claude: F10 — сброс
    f10_request = false;
    zx_machine_reset();
    ay_reset();
    fdc_reset();                     // Claude: сброс сбрасывает и ВГ93 (диски остаются вставленными)
    Serial.println("[reset]");
  }
  if (f11_request) {                 // Claude: F11 — Magic (NMI), см. run_frame()
    f11_request = false;
    magic_press();
  }
  if (f9_request) {                  // Claude: F9 — меню дисков (эмуляция на паузе)
    f9_request = false;
    disk_menu_run();
    next_us = esp_timer_get_time();  // пауза не считается отставанием
  }
  if (f12_request) {
    f12_request = false;
    tape_rewind();
    inj_start_load();
#ifdef TAPE_NAME
    Serial.println("[tape] rewind + LOAD \"\" (" TAPE_NAME ")");
#else
    Serial.println("[tape] no tape.h - tape is empty (Esc = BREAK)");
#endif
  }

  // --- Темп 50 Гц по часам ---
  // Claude: Ждём до назначенного момента следующего кадра. Длинную часть ожидания
  //   отдаём системе (vTaskDelay), короткую досиживаем в цикле.
#if VGA_HZ == 50
  // Claude (01.10.2026): VGA 50 Гц — ждём кадровый импульс монитора (on_vsync). Кадр Z80,
  //   посчитанный после импульса N, VGA покажет с импульса N+1 — всегда, без пропусков.
  //   40 мс — страховка: если видео не запустилось, эмуляция всё равно идёт (~25 кадров/с).
  if (video_status == 1) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(40)) == 0) late++;
    next_us = esp_timer_get_time();
  } else
#endif
  {
  next_us += FRAME_US;
  int64_t now = esp_timer_get_time();
  while (now < next_us) {
    if (next_us - now > 2000) vTaskDelay(1);
    now = esp_timer_get_time();
  }
  // Claude: Если отстали больше чем на 5 кадров (не успеваем эмулировать) — не
  //   пытаемся "догонять", а сбрасываем расписание.
  if (now - next_us > 5 * FRAME_US) { next_us = now; late++; }
  }

  // --- Раз в секунду: статистика ---
  uint32_t ms = millis();
  if (ms - last_print_ms >= 1000) {
    // Claude: FRAMES — системная переменная ROM по адресу 23672 (0x5C78), 3 байта.
    //   Её увеличивает сам ROM в обработчике прерывания — это честная проверка,
    //   что прерывания доходят и ROM работает.
    uint32_t frames_sysvar = zx_peek(0x5C78) | (zx_peek(0x5C79) << 8) | (zx_peek(0x5C7A) << 16);
    uint32_t avg_us = frames_in_sec ? (uint32_t)(emu_us_acc / frames_in_sec) : 0;
    Serial.printf("fps=%u  FRAMES +%u  emu=%u us/frame (load %u%%)  PC=%04X  border=%u  late=%u  snd_underruns=%u snd_dropped=%u\n",
                  (unsigned)frames_in_sec, (unsigned)(frames_sysvar - last_frames_sysvar),
                  (unsigned)avg_us, (unsigned)(avg_us * 100 / FRAME_US),
                  (unsigned)(z80.pc & 0xFFFF), (unsigned)zx_border, (unsigned)late,
                  (unsigned)audio_underruns, (unsigned)audio_dropped);
    Serial.printf("  bounce gap: min %u us, max %u us (norm %u us), buffer fixes %u\n",   // Claude: отладка видео
                  (unsigned)(bb_gap_min / 240), (unsigned)(bb_gap_max / 240),
                  (unsigned)(BB_LINES * (VGA_HZ == 50 ? 768 : 763) / 24), (unsigned)bb_fixes);   // Claude (01.10.2026): длина строки зависит от VGA_HZ
    bb_gap_max = 0; bb_gap_min = 0xFFFFFFFF;
    last_frames_sysvar = frames_sysvar;
    frames_in_sec = 0;
    emu_us_acc = 0;
    last_print_ms = ms;
  }

  // Claude: Через ~3 с после старта один раз печатаем ASCII-скриншот.
  if (!shot_done && zx_frame >= 150) { shot_done = true; print_ascii_screen(); }
}
