/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 * Compability with board caches, Ulf Carlsson
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/sgialib.h>
#include <asm/bcache.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/major.h>

#ifdef CONFIG_ARC_CONSOLE
#define __init
#endif

/*
 * IP22 boardcache is not compatible with board caches.  Thus we disable it
 * during romvec action.  Since r4xx0.c is always compiled and linked with your
 * kernel, this shouldn't cause any harm regardless what MIPS processor you
 * have.
 *
 * The ARC write and read functions seem to interfere with the serial lines
 * in some way. You should be careful with them.
 */

void __init prom_putchar(char c)
{
	long cnt;
	char it = c;

	bc_disable();
	romvec->write(1, &it, 1, &cnt);
	bc_enable();
}

char __init prom_getchar(void)
{
	long cnt;
	char c;

	bc_disable();
	romvec->read(0, &c, 1, &cnt);
	bc_enable();

	return c;
}

static char ppbuf[1024];

void __init prom_printf(char *fmt, ...)
{
	va_list args;
	char ch, *bptr;
	int i;

	va_start(args, fmt);
	i = vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

	while ((ch = *(bptr++)) != 0) {
		if (ch == '\n')
			prom_putchar('\r');

		prom_putchar(ch);
	}
	va_end(args);
}

static void
arc_console_write(struct console *con, const char *s, unsigned n)
{
	prom_printf("%s", s);
}

static kdev_t 
arc_console_dev(struct console *c)
{
	return MKDEV(TTY_MAJOR, 64 + c->index);
}

static struct console arc_prom_console = {
    name:	"prom",
    write:	arc_console_write,
    device:	arc_console_dev,
    flags:	CON_PRINTBUFFER,
    index:	-1,
};

__init void arc_setup_console(void)
{
	register_console(&arc_prom_console);
}
