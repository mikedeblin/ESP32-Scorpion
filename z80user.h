/* z80user.h — память и порты для z80emu: Scorpion ZS-256
 *
 * Claude: Версия для Скорпиона. Память Z80 (64 КБ адресов) — это 4 "окна" по 16 КБ:
 *   окно 0 (0000-3FFF): страница ROM (или ОЗУ 0 при #1FFD.D0)
 *   окно 1 (4000-7FFF): ОЗУ 5,  окно 2 (8000-BFFF): ОЗУ 2
 *   окно 3 (C000-FFFF): ОЗУ 0..15 по #7FFD/#1FFD
 *   zx_rd[окно] — откуда читать, zx_wr[окно] — куда писать (NULL = ROM, запись игнорируется).
 *   Таблицы обновляет zx_remap() в ESP32-Scorpion.ino при записи в порты и смене DOS/.
 *
 * Claude: Сигнал DOS/ (TR-DOS / теневой монитор) отслеживается при ВЫБОРКЕ из памяти
 *   по адресу PC (Z80_FETCH_BYTE — это опкоды и операнды команды):
 *     выборка из ОЗУ (>= #4000) при активном DOS  -> DOS выключается;
 *     выборка из #3Dxx при подключённом Basic 48  -> DOS включается (как Beta 128).
 *   Обычные чтения/записи данных (Z80_READ_BYTE) DOS не трогают.
 *
 * Claude: Порты. z80emu передаёт в port только МЛАДШИЙ байт (n или C). Старший берём
 *   из регистров: для IN A,(n) / OUT (n),A это A, для команд с (C) — B. Какая это
 *   команда, различаем по имени аргумента (#port -> "n" или "C"), это константа
 *   времени компиляции. Нюанс z80emu: в INI/INIR/OUTI/OTIR старший байт — значение B
 *   ДО уменьшения (реальный Z80 для OUTI выставляет уже уменьшенный B). Для портов
 *   Скорпиона (#7FFD/#1FFD/#FFFD/#BFFD) блочными командами никто не пользуется.
 *   Сама логика портов — zx_port_in()/zx_port_out() в ESP32-Scorpion.ino.
 */

#ifndef __Z80USER_INCLUDED__
#define __Z80USER_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

/* Claude: определены в ESP32-Scorpion.ino (с extern "C") */
extern unsigned char *zx_rd[4];
extern unsigned char *zx_wr[4];
extern unsigned char  zx_dos;            /* 1 = активен DOS/ (ROM Monitor/TR-DOS)      */
extern unsigned char  zx_rom_page;       /* текущая страница ROM в окне 0 (0..3)       */
extern unsigned char  zx_rom0_is_rom;    /* 1 = в окне 0 ROM, 0 = ОЗУ 0 (#1FFD.D0)     */
extern void           zx_dos_set(int on);
extern unsigned char  zx_port_in(unsigned int port, int t);   /* Claude: t — такт кадра (нужен ВГ93) */
extern void           zx_port_out(unsigned int port, unsigned char value, int t);
extern int            zx_tbase;          /* такт кадра, с которого начат текущий Z80Emulate */

#define Z80_READ_BYTE(address, x)                                       \
{                                                                       \
        unsigned int zx_ra_ = (address) & 0xffff;                       \
        (x) = zx_rd[zx_ra_ >> 14][zx_ra_ & 0x3fff];                     \
}

/* Claude: выборка по PC — здесь же переключение DOS/ */
#define Z80_FETCH_BYTE(address, x)                                      \
{                                                                       \
        unsigned int zx_fa_ = (address) & 0xffff;                       \
        if (zx_fa_ >= 0x4000) {                                         \
                if (zx_dos) zx_dos_set(0);                              \
        } else if ((zx_fa_ & 0xff00) == 0x3d00 && !zx_dos               \
                   && zx_rom_page == 1 && zx_rom0_is_rom) {             \
                zx_dos_set(1);                                          \
        }                                                               \
        (x) = zx_rd[zx_fa_ >> 14][zx_fa_ & 0x3fff];                     \
}

#define Z80_READ_WORD(address, x)                                       \
{                                                                       \
        unsigned int zx_ra_ = (address) & 0xffff;                       \
        unsigned int zx_rb_ = ((address) + 1) & 0xffff;                 \
        (x) = zx_rd[zx_ra_ >> 14][zx_ra_ & 0x3fff]                      \
            | (zx_rd[zx_rb_ >> 14][zx_rb_ & 0x3fff] << 8);              \
}

#define Z80_FETCH_WORD(address, x)      Z80_READ_WORD((address), (x))

/* Claude: запись; NULL в zx_wr = окно с ROM, запись игнорируется */
#define Z80_WRITE_BYTE(address, x)                                      \
{                                                                       \
        unsigned int zx_wa_ = (address) & 0xffff;                       \
        unsigned char *zx_wp_ = zx_wr[zx_wa_ >> 14];                    \
        if (zx_wp_) zx_wp_[zx_wa_ & 0x3fff] = (unsigned char)(x);       \
}

#define Z80_WRITE_WORD(address, x)                                      \
{                                                                       \
        unsigned int zx_wa_ = (address) & 0xffff;                       \
        unsigned int zx_wb_ = ((address) + 1) & 0xffff;                 \
        unsigned char *zx_wp_ = zx_wr[zx_wa_ >> 14];                    \
        if (zx_wp_) zx_wp_[zx_wa_ & 0x3fff] = (unsigned char)(x);       \
        zx_wp_ = zx_wr[zx_wb_ >> 14];                                   \
        if (zx_wp_) zx_wp_[zx_wb_ & 0x3fff] = (unsigned char)((x) >> 8);\
}

#define Z80_READ_WORD_INTERRUPT(address, x)     Z80_READ_WORD((address), (x))
#define Z80_WRITE_WORD_INTERRUPT(address, x)    Z80_WRITE_WORD((address), (x))

/* Claude: старший байт адреса порта: A для (n), B для (C) — см. шапку */
#define ZX_PORT_HI(port)  ((#port[0] == 'n') ? (unsigned int)A : (unsigned int)B)

/* Claude: второй аргумент — момент чтения в тактах кадра (как у OUT). По нему ВГ93
 *   считает индексный импульс диска. */
#define Z80_INPUT_BYTE(port, x)                                         \
{                                                                       \
        (x) = zx_port_in((ZX_PORT_HI(port) << 8) | ((port) & 0xff),     \
                         zx_tbase + elapsed_cycles);                    \
}

/* Claude: третий аргумент — момент записи в тактах от начала кадра. elapsed_cycles —
 *   локальный счётчик внутри emulate() в z80emu.c (макрос разворачивается там же),
 *   zx_tbase — сколько тактов кадра прошло до текущего вызова Z80Emulate. Нужен для
 *   звука: бипер и AY синтезируются по событиям с точными моментами. */
#define Z80_OUTPUT_BYTE(port, x)                                        \
{                                                                       \
        zx_port_out((ZX_PORT_HI(port) << 8) | ((port) & 0xff),          \
                    (unsigned char)(x), zx_tbase + elapsed_cycles);     \
}

#ifdef __cplusplus
}
#endif

#endif
