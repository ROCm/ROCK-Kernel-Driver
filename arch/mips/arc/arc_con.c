/*
 * Wrap-around code for a console using the
 * ARC io-routines.
 *
 * Copyright (c) 1998 Harald Koerfgen 
 */

#include <linux/tty.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/fs.h>

extern char prom_getchar (void);
extern void prom_printf (char *, ...);

static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
	unsigned i;

	/*
	 *    Now, do each character
	 */
	for (i = 0; i < count; i++) {
		if (*s == 10)
			prom_printf("%c", 13);
		prom_printf("%c", *s++);
	}
}

static int prom_console_wait_key(struct console *co)
{
	return prom_getchar();
}

static int __init prom_console_setup(struct console *co, char *options)
{
	return 0;
}

static kdev_t prom_console_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, 64 + c->index);
}

static struct console arc_cons = {
	"ttyS",
	prom_console_write,
	NULL,
	prom_console_device,
	prom_console_wait_key,
	NULL,
	prom_console_setup,
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};

/*
 *    Register console.
 */

void __init arc_console_init(void)
{
	register_console(&arc_cons);
}
