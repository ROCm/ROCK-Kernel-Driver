/*
 * linux/include/asm-arm/arch-shark/keyboard.h
 * by Alexander.Schulz@stud.uni-karlsruhe.de
 * 
 * Derived from linux/include/asm-arm/arch-ebsa285/keyboard.h
 * (C) 1998 Russell King
 * (C) 1998 Phil Blundell
 */
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];

#define KEYBOARD_IRQ			IRQ_ISA_KEYBOARD

#define NR_SCANCODES 128

#define kbd_setkeycode(sc,kc)		pckbd_setkeycode(sc,kc)
#define kbd_getkeycode(sc)		pckbd_getkeycode(sc)
#define kbd_translate(sc, kcp, rm)	pckbd_translate(sc, kcp, rm)
#define kbd_unexpected_up		pckbd_unexpected_up
#define kbd_leds(leds)			pckbd_leds(leds)
#define kbd_init_hw()			pckbd_init_hw()
#define kbd_sysrq_xlate			pckbd_sysrq_xlate

#define kbd_disable_irq()
#define kbd_enable_irq()

#define SYSRQ_KEY	0x54

/* resource allocation */
#define kbd_request_region() request_region(0x60, 16, "keyboard")
#define kbd_request_irq(handler) request_irq(KEYBOARD_IRQ, handler, 0, \
                                             "keyboard", NULL)

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input() inb(KBD_DATA_REG)
#define kbd_read_status() inb(KBD_STATUS_REG)
#define kbd_write_output(val) outb(val, KBD_DATA_REG)
#define kbd_write_command(val) outb(val, KBD_CNTL_REG)

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

/*
 * Machine specific bits for the PS/2 driver
 */
#define aux_request_irq(hand, dev_id)					\
	request_irq(AUX_IRQ, hand, SA_SHIRQ, "PS/2 Mouse", dev_id)

#define aux_free_irq(dev_id) free_irq(AUX_IRQ, dev_id)
