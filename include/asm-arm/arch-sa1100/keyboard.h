/*
 *  linux/include/asm-arm/arch-sa1100/keyboard.h
 *  Created 16 Dec 1999 by Nicolas Pitre <nico@cam.org>
 *  This file contains the SA1100 architecture specific keyboard definitions
 */

#ifndef _SA1100_KEYBOARD_H
#define _SA1100_KEYBOARD_H

#include <linux/config.h>


// #ifdef CONFIG_SA1100_BRUTUS
/* need fixing... */
#if 0

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

#elif CONFIG_SA1100_GRAPHICSCLIENT
extern int gc_kbd_translate(unsigned char scancode, unsigned char *keycode, char raw_mode);
extern void gc_kbd_leds(unsigned char leds);
extern void gc_kbd_init_hw(void);
extern void gc_kbd_enable_irq(void);
extern void gc_kbd_disable_irq(void);
extern unsigned char gc_kbd_sysrq_xlate[128];

#define kbd_setkeycode(x...)    (-ENOSYS)
#define kbd_getkeycode(x...)    (-ENOSYS)
#define kbd_translate           gc_kbd_translate
#define kbd_unexpected_up(x...) (1)
#define kbd_leds                gc_kbd_leds
#define kbd_init_hw             gc_kbd_init_hw
#define kbd_enable_irq          gc_kbd_enable_irq
#define kbd_disable_irq         gc_kbd_disable_irq
#define kbd_sysrq_xlate         gc_kbd_sysrq_xlate

#elif CONFIG_SA1100_BITSY

#define kbd_setkeycode(x...)    (-ENOSYS)
#define kbd_getkeycode(x...)    (-ENOSYS)
#define kbd_translate(sc_,kc_,rm_)	((*(kc_)=(sc_)),1)
#define kbd_unexpected_up(x...) (1)
#define kbd_leds(x...)		do { } while (0)
#define kbd_init_hw(x...)	do { } while (0)
#define kbd_enable_irq(x...)	do { } while (0)
#define kbd_disable_irq(x...)	do { } while (0)

#elif 0 //defined(CONFIG_SA1111)   /*@@@@@*/

#define KEYBOARD_IRQ           TPRXINT
#define DISABLE_KBD_DURING_INTERRUPTS  0

/* redefine some macros */
#ifdef KBD_DATA_REG
#undef KBD_DATA_REG
#endif
#ifdef KBD_STATUS_REG
#undef KBD_STATUS_REG
#endif
#ifdef KBD_CNTL_REG
#undef KBD_CNTL_REG
#endif
#define KBD_DATA_REG           KBDDATA
#define KBD_STATUS_REG         KBDSTAT
#define KBD_CNTL_REG           KBDCR

extern int sa1111_setkeycode(unsigned int scancode, unsigned int keycode);
extern int sa1111_getkeycode(unsigned int scancode);
extern int sa1111_translate(unsigned char scancode, unsigned char *keycode,
				char raw_mode);
extern char sa1111_unexpected_up(unsigned char keycode);
extern void sa1111_leds(unsigned char leds);
extern void sa1111_init_hw(void);
extern unsigned char sa1111_sysrq_xlate[128];

#define kbd_setkeycode		sa1111_setkeycode
#define kbd_getkeycode		sa1111_getkeycode
#define kbd_translate		sa1111_translate
#define kbd_unexpected_up	sa1111_unexpected_up
#define kbd_leds		sa1111_leds
#define kbd_init_hw		sa1111_init_hw
#define kbd_sysrq_xlate		sa1111_sysrq_xlate
#define kbd_disable_irq(x...)	do{;}while(0)
#define kbd_enable_irq(x...)	do{;}while(0)

#define SYSRQ_KEY 0x54

/* resource allocation */
#define kbd_request_region()
#define kbd_request_irq(handler) request_irq(KEYBOARD_IRQ, handler, 0, \
					"keyboard", NULL)

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input()	(*KBDDATA & 0x00ff)
#define kbd_read_status()	(*KBDSTAT & 0x01ff)
#define kbd_write_output(val)	(*KBDDATA = (val))
#define kbd_write_command(val)	(*KBDCR = (val))

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do {;} while(0)

/* bit definitions for some registers */
#define KBD_CR_ENA	(1<<3)

#define KBD_STAT_RXB	(1<<4)
#define KBD_STAT_RXF	(1<<5)
#define KBD_STAT_TXB	(1<<6)
#define KBD_STAT_TXE	(1<<7)
#define KBD_STAT_STP	(1<<8)

/*
 * Machine specific bits for the PS/2 driver
 */

#define AUX_IRQ		MSRXINT

#define aux_request_irq(hand, dev_id)  \
		request_irq(AUX_IRQ, hand, SA_SHIRQ, "PS/2 Mouse", dev_id)
#define aux_free_irq(dev_id) free_irq(AUX_IRQ, dev_id)

/* How to access the mouse macros on this platform.  */
#define mse_read_input()	(*MSEDATA & 0x00ff)
#define mse_read_status()	(*MSESTAT & 0x01ff)
#define mse_write_output(val)	(*MSEDATA = (val))
#define mse_write_command(val)	(*MSECR = (val))

/* bit definitions for some registers */
#define MSE_CR_ENA	(1<<3)

#define MSE_STAT_RXB	(1<<4)
#define MSE_STAT_RXF	(1<<5)
#define MSE_STAT_TXB	(1<<6)
#define MSE_STAT_TXE	(1<<7)
#define MSE_STAT_STP	(1<<8)


#else

/* dummy i.e. no real keyboard */
#define kbd_setkeycode(x...)	(-ENOSYS)
#define kbd_getkeycode(x...)	(-ENOSYS)
#define kbd_translate(x...)	(0)
#define kbd_unexpected_up(x...)	(1)
#define kbd_leds(x...)		do {;} while (0)
#define kbd_init_hw(x...)	do {;} while (0)
#define kbd_enable_irq(x...)	do {;} while (0)
#define kbd_disable_irq(x...)	do {;} while (0)

#endif


/* needed if MAGIC_SYSRQ is enabled for serial console */
#ifndef SYSRQ_KEY
#define SYSRQ_KEY		((unsigned char)(-1))
#define kbd_sysrq_xlate         ((unsigned char *)NULL)
#endif


#endif  /* _SA1100_KEYBOARD_H */

