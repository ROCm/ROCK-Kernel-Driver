/* $Id: printf.c,v 1.1 1999/08/20 21:13:33 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Putting things on the screen using SGI arcs PROM facilities.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/sgialib.h>

static char ppbuf[1024];

void prom_printf(char *fmt, ...)
{
	va_list args;
	char ch, *bptr;
	int i;

	va_start(args, fmt);
	i = vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

	while((ch = *(bptr++)) != 0) {
		if(ch == '\n')
			prom_putchar('\r');

		prom_putchar(ch);
	}
	va_end(args);
	return;
}
