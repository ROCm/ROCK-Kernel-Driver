/*
 * Kernel Debugger Console I/O handler
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Scott Lurndal (slurn@engr.sgi.com)
 * Copyright (C) Scott Foehner (sfoehner@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 *
 * See the file LIA-COPYRIGHT for additional information.
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *	Chuck Fleckenstein		1999/07/20
 *		Move kdb_info struct declaration to this file
 *		for cases where serial support is not compiled into
 *		the kernel.
 *
 *	Masahiro Adegawa		1999/07/20
 *		Handle some peculiarities of japanese 86/106
 *		keyboards.
 *
 *	marc@mucom.co.il		1999/07/20
 *		Catch buffer overflow for serial input.
 *
 *      Scott Foehner
 *              Port to ia64
 *
 *	Scott Lurndal			2000/01/03
 *		Restructure for v1.0
 *
 *	Keith Owens			2000/05/23
 *		KDB v1.2
 *
 *	Andi Kleen			2000/03/19
 *		Support simultaneous input from serial line and keyboard.
 */

#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/keyboard.h>
#include <linux/serial_reg.h>

#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <asm/machdep.h>
#undef FILE

int kdb_port;

struct kdb_serial kdb_serial;
/*{
	int io_type;
	unsigned long iobase;
	unsigned long ioreg_shift;
} kdb_serial_t;
*/

int inchar(void);


char *
kdba_read(char *buffer, size_t bufsize)
{
	char	*cp = buffer;
	char	*bufend = buffer+bufsize-2;	/* Reserve space for newline and null byte */

	for (;;) {
	    unsigned char key = ppc_md.udbg_getc();
		/* Echo is done in the low level functions */
		switch (key) {
		case '\b': /* backspace */
		case '\x7f': /* delete */
			if (cp > buffer) {
				udbg_puts("\b \b");
				--cp;
			}
			break;
		case '\n': /* enter */
		case '\r': /* - the other enter... */
			ppc_md.udbg_putc('\n');
			*cp++ = '\n';
			*cp++ = '\0';
			return buffer;
		case '\x00': /* check for NULL from udbg_getc */
		        break;
		default:
			if (cp < bufend)
			ppc_md.udbg_putc(key);
				*cp++ = key;
			break;
		}
	}
}



