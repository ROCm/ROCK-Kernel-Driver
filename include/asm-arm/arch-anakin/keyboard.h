/*
 *  linux/include/asm-arm/arch-anakin/keyboard.h
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   11-Apr-2001 TTC	Created
 */

#ifndef __ASM_ARCH_KEYBOARD_H
#define __ASM_ARCH_KEYBOARD_H

#define kbd_setkeycode(s, k)	(-EINVAL)
#define kbd_getkeycode(s)	(-EINVAL)
#define kbd_translate(s, k, r)	0
#define kbd_unexpected_up(k)	0
#define kbd_leds(l)
#define kbd_init_hw()
#define kbd_sysrq_xlate		((int *) 0)
#define kbd_disable_irq()
#define kbd_enable_irq()

#define SYSRQ_KEY		0x54

#endif
