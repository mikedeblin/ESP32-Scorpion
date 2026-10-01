/* z80config.h — ZX Spectrum 48K on ESP32-S3
 * HALT is NOT caught: BASIC uses HALT+JR loop to wait for interrupts.
 */

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

#endif
