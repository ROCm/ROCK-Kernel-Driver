/*
 *  linux/include/asm-arm/arch-sa1100/keyboard.h
 *  Created 16 Dec 1999 by Nicolas Pitre <nico@cam.org>
 *  This file contains the SA1100 architecture specific keyboard definitions
 */

#ifndef _SA1100_KEYBOARD_H
#define _SA1100_KEYBOARD_H

#include <linux/config.h>


#ifdef CONFIG_SA1100_BRUTUS

extern int Brutus_kbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern void Brutus_kbd_leds(unsigned char leds);
extern void Brutus_kbd_init_hw(void);
extern void Brutus_kbd_enable_irq(void);
extern void Brutus_kbd_disable_irq(void);
extern unsigned char Brutus_kbd_sysrq_xlate[128];

#define kbd_setkeycode(x...)	(-ENOSYS)
#define kbd_getkeycode(x...)	(-ENOSYS)
#define kbd_translate		Brutus_kbd_translate
#define kbd_unexpected_up(x...)	(1)
#define kbd_leds		Brutus_kbd_leds
#define kbd_init_hw		Brutus_kbd_init_hw
#define kbd_enable_irq		Brutus_kbd_enable_irq
#define kbd_disable_irq		Brutus_kbd_disable_irq
#define kbd_sysrq_xlate		Brutus_kbd_sysrq_xlate

#define SYSRQ_KEY 0x54

#else

/* dummy i.e. no real keyboard */
#define kbd_setkeycode(x...)	(-ENOSYS)
#define kbd_getkeycode(x...)	(-ENOSYS)
#define kbd_translate(x...)	(0)
#define kbd_unexpected_up(x...)	(1)
#define kbd_leds(x...)		do { } while (0)
#define kbd_init_hw(x...)	do { } while (0)
#define kbd_enable_irq(x...)	do { } while (0)
#define kbd_disable_irq(x...)	do { } while (0)

#endif


#endif  /* _SA1100_KEYBOARD_H */

