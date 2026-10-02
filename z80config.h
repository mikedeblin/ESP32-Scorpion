/* z80config.h — Scorpion ZS-256 on ESP32-S3 */

#ifndef __Z80CONFIG_INCLUDED__
#define __Z80CONFIG_INCLUDED__

/* Claude: Останавливаться на неопределённых командах ED xx. Нужно для перехвата
 *   загрузки с ленты: в копию ROM по адресу LD-BYTES (#0556) ставится ED 00, ядро
 *   останавливается с PC = #0556, и загрузку выполняет C-код (см. tape_trap()).
 *   Остальные неопределённые ED xx эмулятор сам пропускает как NOP (как реальный Z80). */
#define Z80_CATCH_ED_UNDEFINED

/* Claude: Ошибка в z80emu: при перехвате z80emu.c пишет статус
 *   Z80_STATUS_FLAG_ED_UNDEFINED, а в z80emu.h он называется Z80_STATUS_ED_UNDEFINED
 *   (без FLAG_). Без этого определения сборка с Z80_CATCH_ED_UNDEFINED падает.
 *   Исправляем здесь, чтобы не трогать сам z80emu.c. */
#define Z80_STATUS_FLAG_ED_UNDEFINED    Z80_STATUS_ED_UNDEFINED

/* Claude (01.10.2026): ЧТО: останавливаться на HALT (статус Z80_STATUS_HALT).
 *   ПОЧЕМУ: без этого z80emu делает HALT так — "съедает" такты только до конца ТЕКУЩЕГО
 *   вызова Z80Emulate, PC уже стоит после HALT. Пока кадр был одним вызовом, это было
 *   верно. С v19 кадр режется на строки (снимок экрана по лучу), и процессор
 *   "просыпался" бы на границе строки без прерывания. Теперь run_frame() сам держит
 *   HALT до принятого прерывания (или NMI).
 *   Та же ошибка имён, что выше: в z80emu.c — FLAG_HALT, в z80emu.h — Z80_STATUS_HALT. */
#define Z80_CATCH_HALT
#define Z80_STATUS_FLAG_HALT            Z80_STATUS_HALT

#endif
