/*
 *  linux/include/asm-arm/arch-sa1100/keyboard.h
 *  Created 16 Dec 1999 by Nicolas Pitre <nico@cam.org>
 *  This file contains the SA1100 architecture specific keyboard definitions
 */

#ifndef _SA1100_KEYBOARD_H
#define _SA1100_KEYBOARD_H

#include <linux/config.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>

extern struct kbd_ops_struct *kbd_ops;

#define kbd_disable_irq()	do { } while(0);
#define kbd_enable_irq()	do { } while(0);


/*
 * SA1111 keyboard driver
 */
extern void sa1111_kbd_init_hw(void);

/*
 * GraphicsClient keyboard driver
 */
extern void gc_kbd_init_hw(void);

static inline void kbd_init_hw(void)
{
	if (machine_is_assabet() && machine_has_neponset())
		sa1111_kbd_init_hw();

	if (machine_is_graphicsclient())
		gc_kbd_init_hw();
}



#if 0	 /* Brutus needs fixing */

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

#elif 0 // CONFIG_SA1100_GRAPHICSCLIENT
extern int gc_kbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int gc_kbd_getkeycode(unsigned int scancode);
extern int gc_kbd_translate(unsigned char scancode, unsigned char *keycode, char raw_mode);
extern void gc_kbd_leds(unsigned char leds);
extern void gc_kbd_init_hw(void);
extern void gc_kbd_enable_irq(void);
extern void gc_kbd_disable_irq(void);
extern unsigned char gc_kbd_sysrq_xlate[128];

#define kbd_setkeycode(x...)    gc_kbd_setkeycode	//(-ENOSYS)
#define kbd_getkeycode(x...)    gc_kbd_getkeycode	//(-ENOSYS)
#define kbd_translate           gc_kbd_translate
#define kbd_unexpected_up(x...) (1)
#define kbd_leds                gc_kbd_leds
#define kbd_init_hw             gc_kbd_init_hw
#define kbd_enable_irq          gc_kbd_enable_irq
#define kbd_disable_irq         gc_kbd_disable_irq
#define kbd_sysrq_xlate         (1)

#endif

#endif  /* _SA1100_KEYBOARD_H */

