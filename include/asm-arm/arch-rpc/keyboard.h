/*
 *  linux/include/asm-arm/arch-rpc/keyboard.h
 *
 *  Copyright (C) 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Keyboard driver definitions for RiscPC architecture
 */
#include <asm/irq.h>

#define NR_SCANCODES 128

extern void ps2kbd_leds(unsigned char leds);
extern void ps2kbd_init_hw(void);
extern unsigned char ps2kbd_sysrq_xlate[NR_SCANCODES];

#define kbd_setkeycode(sc,kc)		(-EINVAL)
#define kbd_getkeycode(sc)		(-EINVAL)

#define kbd_translate(sc, kcp, rm)	({ *(kcp) = (sc); 1; })
#define kbd_unexpected_up(kc)		(0200)
#define kbd_leds(leds)			ps2kbd_leds(leds)
#define kbd_init_hw()			ps2kbd_init_hw()
#define kbd_sysrq_xlate			ps2kbd_sysrq_xlate
#define kbd_disable_irq()		disable_irq(IRQ_KEYBOARDRX)
#define kbd_enable_irq()		enable_irq(IRQ_KEYBOARDRX)

#define SYSRQ_KEY	13
