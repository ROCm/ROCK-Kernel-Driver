/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 *  linux/include/asm-ppc/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 *  Modified for Power Macintosh by Paul Mackerras
 */

/*
 * This file contains the ppc architecture specific keyboard definitions -
 * like the intel pc for prep systems, different for power macs.
 */

#ifndef __ASM_KEYBOARD_H__
#define __ASM_KEYBOARD_H__

#ifdef __KERNEL__

#include <linux/adb.h>
#include <asm/machdep.h>

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/kd.h>
#include <asm/io.h>

#ifndef KEYBOARD_IRQ
#define KEYBOARD_IRQ			1
#endif
#define DISABLE_KBD_DURING_INTERRUPTS	0
#define INIT_KBD

extern int mac_hid_kbd_translate(unsigned char scancode, unsigned char *keycode,
				 char raw_mode);
extern char mac_hid_kbd_unexpected_up(unsigned char keycode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern unsigned char pckbd_sysrq_xlate[128];

static inline int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return 0;
}
  
static inline int kbd_getkeycode(unsigned int scancode)
{
	return 0;
}
  
static inline int kbd_translate(unsigned char keycode, unsigned char *keycodep,
				char raw_mode)
{
	if ( ppc_md.kbd_translate )
		return ppc_md.kbd_translate(keycode, keycodep, raw_mode);
	else
		return pckbd_translate(keycode, keycodep, raw_mode);
}
  
static inline int kbd_unexpected_up(unsigned char keycode)
{
	if ( ppc_md.kbd_unexpected_up )
		return ppc_md.kbd_unexpected_up(keycode);
	else
		return pckbd_unexpected_up(keycode);
}
  
static inline void kbd_leds(unsigned char leds)
{
}

static inline void kbd_init_hw(void)
{
}

#define kbd_sysrq_xlate	pckbd_sysrq_xlate

extern unsigned long SYSRQ_KEY;

/* resource allocation */
#define kbd_request_region()
#define kbd_request_irq(handler) request_irq(KEYBOARD_IRQ, handler, 0, \
                                             "keyboard", NULL)

/* How to access the keyboard macros on this platform.  */
#ifndef kbd_read_input
#define kbd_read_input() inb(KBD_DATA_REG)
#define kbd_read_status() inb(KBD_STATUS_REG)
#define kbd_write_output(val) outb(val, KBD_DATA_REG)
#define kbd_write_command(val) outb(val, KBD_CNTL_REG)
#endif

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

/*
 * Machine specific bits for the PS/2 driver
 */

#ifndef AUX_IRQ	
#define AUX_IRQ 12
#endif

#define aux_request_irq(hand, dev_id)					\
	request_irq(AUX_IRQ, hand, SA_SHIRQ, "PS/2 Mouse", dev_id)

#define aux_free_irq(dev_id) free_irq(AUX_IRQ, dev_id)

#endif /* __KERNEL__ */
#endif /* __ASM_KEYBOARD_H__ */
