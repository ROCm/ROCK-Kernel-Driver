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

#include <linux/config.h>

struct sun_initfunc {
	int			(*init) (void);
	struct sun_initfunc	*next;
};

struct sunserial_operations {
	struct sun_initfunc	*rs_init;
	void		(*rs_kgdb_hook) (int);
	void		(*rs_change_mouse_baud) (int);
	int		(*rs_read_proc) (char *, char **, off_t, int, int *, void *);
};

struct sunkbd_operations {
	struct sun_initfunc	*kbd_init;
	void		(*compute_shiftstate) (void);
	void		(*setledstate) (struct kbd_struct *, unsigned int);
	unsigned char	(*getledstate) (void);
	int		(*setkeycode) (unsigned int, unsigned int);
	int		(*getkeycode) (unsigned int);
};

extern struct sunserial_operations rs_ops;
extern struct sunkbd_operations kbd_ops;

extern void sunserial_setinitfunc(int (*) (void));
extern void sunkbd_setinitfunc(int (*) (void));

extern int serial_console;
extern int stop_a_enabled;
extern void sunserial_console_termios(struct console *);

#ifdef CONFIG_PCI
extern void sunkbd_install_keymaps(ushort **, unsigned int, char *,
				   char **, int, int, struct kbdiacr *,
				   unsigned int);
#endif

#endif /* !(_SERIAL_SUN_H) */
