/*
 * Kernel Debugger Architecture Independent Console I/O handler
 *
 * Copyright (C) 1999-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#ifdef CONFIG_SPARC64
#include <asm/oplib.h>
#else
static struct console *kdbcons;
#endif

/*
 * kdb_read
 *
 *	This function reads a string of characters, terminated by
 *	a newline, or by reaching the end of the supplied buffer,
 *	from the current kernel debugger console device.
 * Parameters:
 *	buffer	- Address of character buffer to receive input characters.
 *	bufsize - size, in bytes, of the character buffer
 * Returns:
 *	Returns a pointer to the buffer containing the received
 *	character string.  This string will be terminated by a
 *	newline character.
 * Locking:
 *	No locks are required to be held upon entry to this
 *	function.  It is not reentrant - it relies on the fact
 *	that while kdb is running on any one processor all other
 *	processors will be spinning at the kdb barrier.
 * Remarks:
 *
 * Davidm asks, why doesn't kdb use the console abstraction;
 * here are some reasons:
 *      - you cannot debug the console abstraction with kdb if
 *        kdb uses it.
 *      - you rely on the correct functioning of the abstraction
 *        in the presence of general system failures.
 *      - You must acquire the console spinlock thus restricting
 *        the usability - what if the kernel fails with the spinlock
 *        held - one still wishes to debug such situations.
 *      - How about debugging before the console(s) are registered?
 *      - None of the current consoles (sercons, vt_console_driver)
 *        have read functions defined.
 *	- The standard pc keyboard and terminal drivers are interrupt
 *	  driven.   We cannot enable interrupts while kdb is active,
 *	  so the standard input functions cannot be used by kdb.
 *
 * An implementation could be improved by removing the need for
 * lock acquisition - just keep a 'struct console *kdbconsole;' global
 * variable which refers to the preferred kdb console.
 *
 * The bulk of this function is architecture dependent.
 */

char *
kdb_read(char *buffer, size_t bufsize)
{
	return(kdba_read(buffer, bufsize));
}

/*
 * kdb_getstr
 *
 *	Print the prompt string and read a command from the
 *	input device.
 *
 * Parameters:
 *	buffer	Address of buffer to receive command
 *	bufsize Size of buffer in bytes
 *	prompt	Pointer to string to use as prompt string
 * Returns:
 *	Pointer to command buffer.
 * Locking:
 *	None.
 * Remarks:
 *	For SMP kernels, the processor number will be
 *	substituted for %d, %x or %o in the prompt.
 */

char *
kdb_getstr(char *buffer, size_t bufsize, char *prompt)
{
#if defined(CONFIG_SMP)
	kdb_printf(prompt, smp_processor_id());
#else
	kdb_printf("%s", prompt);
#endif
	kdb_nextline = 1;	/* Prompt and input resets line number */
	return kdb_read(buffer, bufsize);
}

/*
 * kdb_printf
 *
 *	Print a string to the output device(s).
 *
 * Parameters:
 *	printf-like format and optional args.
 * Returns:
 *	0
 * Locking:
 *	None.
 * Remarks:
 *	use 'kdbcons->write()' to avoid polluting 'log_buf' with
 *	kdb output.
 */

static char kdb_buffer[256];	/* A bit too big to go on stack */

void
kdb_printf(const char *fmt, ...)
{
	va_list	ap;
	int diag;
	int linecount;
	int logging, saved_loglevel = 0;
	int do_longjmp = 0;
	struct console *c = console_drivers;
	static spinlock_t kdb_printf_lock = SPIN_LOCK_UNLOCKED;

	/* Serialize kdb_printf if multiple cpus try to write at once.
	 * But if any cpu goes recursive in kdb, just print the output,
	 * even if it is interleaved with any other text.
	 */
	if (!KDB_STATE(PRINTF_LOCK)) {
		KDB_STATE_SET(PRINTF_LOCK);
		spin_lock(&kdb_printf_lock);
	}

	diag = kdbgetintenv("LINES", &linecount);
	if (diag || linecount <= 1)
		linecount = 22;

	diag = kdbgetintenv("LOGGING", &logging);
	if (diag)
		logging = 0;

	va_start(ap, fmt);
	vsnprintf(kdb_buffer, sizeof(kdb_buffer), fmt, ap);
	va_end(ap);

	/*
	 * Write to all consoles.
	 */
#ifdef CONFIG_SPARC64
	if (c == NULL)
		prom_printf("%s", kdb_buffer);
	else
#endif
	while (c) {
		c->write(c, kdb_buffer, strlen(kdb_buffer));
		c = c->next;
	}
	if (logging) {
		saved_loglevel = console_loglevel;
		console_loglevel = 0;
		printk("%s", kdb_buffer);
	}

	if (strchr(kdb_buffer, '\n') != NULL) {
		kdb_nextline++;
	}

	if (kdb_nextline == linecount) {
#ifdef KDB_HAVE_LONGJMP
		char buf1[16];
#if defined(CONFIG_SMP)
		char buf2[32];
#endif
		char *moreprompt;

		/* Watch out for recursion here.  Any routine that calls
		 * kdb_printf will come back through here.  And kdb_read
		 * uses kdb_printf to echo on serial consoles ...
		 */
		kdb_nextline = 1;	/* In case of recursion */

		/*
		 * Pause until cr.
		 */
		moreprompt = kdbgetenv("MOREPROMPT");
		if (moreprompt == NULL) {
			moreprompt = "more> ";
		}

#if defined(CONFIG_SMP)
		if (strchr(moreprompt, '%')) {
			sprintf(buf2, moreprompt, smp_processor_id());
			moreprompt = buf2;
		}
#endif

		c = console_drivers;
#ifdef CONFIG_SPARC64
		if (c == NULL)
			prom_printf("%s", moreprompt);
		else
#endif
		while (c) {
			c->write(c, moreprompt, strlen(moreprompt));
			c = c->next;
		}
		if (logging)
			printk("%s", moreprompt);

		kdb_read(buf1, sizeof(buf1));
		kdb_nextline = 1;	/* Really set output line 1 */

		if ((buf1[0] == 'q') || (buf1[0] == 'Q'))
			do_longjmp = 1;
		else if (buf1[0] && buf1[0] != '\n')
			kdb_printf("Only 'q' or 'Q' are processed at more prompt, input ignored\n");
#endif	/* KDB_HAVE_LONGJMP */
	}

	if (logging) {
		console_loglevel = saved_loglevel;
	}
	if (KDB_STATE(PRINTF_LOCK)) {
		spin_unlock(&kdb_printf_lock);
		KDB_STATE_CLEAR(PRINTF_LOCK);
	}
	if (do_longjmp)
#ifdef KDB_HAVE_LONGJMP
		kdba_longjmp(&kdbjmpbuf[smp_processor_id()], 1);
#else
		;
#endif	/* KDB_HAVE_LONGJMP */
}

/*
 * kdb_io_init
 *
 *	Initialize kernel debugger output environment.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	Select a console device.
 */

void __init
kdb_io_init(void)
{
#ifndef CONFIG_SPARC64 /* we don't register serial consoles in time */
	/*
 	 * Select a console.
 	 */
	struct console *c = console_drivers;

	while (c) {
		if ((c->flags & CON_CONSDEV)) {
			kdbcons = c;
			break;
		}
		c = c->next;
	}

	if (kdbcons == NULL) {
		long long i;

		printk("kdb: Initialization failed - no console\n");
		while (1) i++;
	}
#endif
	return;
}

EXPORT_SYMBOL(kdb_read);
