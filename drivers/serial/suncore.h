/* suncore.h
 *
 * Generic SUN serial/kbd/ms layer.  Based entirely
 * upon drivers/sbus/char/sunserial.h which is:
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 *
 * Port to new UART layer is:
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 */

#ifndef _SERIAL_SUN_H
#define _SERIAL_SUN_H

/* Serial keyboard defines for L1-A processing... */
#define SUNKBD_RESET		0xff
#define SUNKBD_L1		0x01
#define SUNKBD_UP		0x80
#define SUNKBD_A		0x4d

extern void sun_do_break(void);

extern unsigned int suncore_mouse_baud_cflag_next(unsigned int, int *);
extern int suncore_mouse_baud_detection(unsigned char, int);

extern struct pt_regs *kbd_pt_regs;

extern int serial_console;
extern int stop_a_enabled;
extern int sunserial_current_minor;

static __inline__ int con_is_present(void)
{
	return serial_console ? 0 : 1;
}

extern void sunserial_console_termios(struct console *);

#endif /* !(_SERIAL_SUN_H) */
