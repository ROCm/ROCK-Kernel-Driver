/* $Id: keyboard.h,v 1.6 2002/01/08 16:00:20 davem Exp $
 * linux/include/asm-sparc64/keyboard.h
 *
 * Created Aug 29 1997 by Eddie C. Dost (ecd@skynet.be)
 */

/*
 *  This file contains the Ultra/PCI architecture specific keyboard definitions
 */

#ifndef _SPARC64_KEYBOARD_H
#define _SPARC64_KEYBOARD_H 1

#ifdef __KERNEL__

/* We use the generic input layer for keyboard handling, thus
 * some of this stuff should never be invoked.
 */
#define kbd_setkeycode(scancode, keycode)	(BUG(), 0)
#define kbd_getkeycode(scancode)		(BUG(), 0)

#define kbd_translate(keycode, keycodep, raw_mode) \
	({ *(keycodep) = scancode; 1; })
#define kbd_unexpected_up(keycode)		(0200)

#define kbd_leds(leds)				do { } while (0)
#define kbd_init_hw()				do { } while (0)

#define SYSRQ_KEY 0x54
extern unsigned char kbd_sysrq_xlate[128];

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_KEYBOARD_H) */
