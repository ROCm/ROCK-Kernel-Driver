/*
 * Kernel Debugger Architecture Independent Console I/O handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/nmi.h>
#include <linux/delay.h>

#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/kallsyms.h>

static struct console *kdbcons;

#ifdef CONFIG_PPC64
#include <asm/udbg.h>
#endif

#define CMD_BUFLEN 256
char kdb_prompt_str[CMD_BUFLEN];

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
 *	  kdb uses it.
 *      - you rely on the correct functioning of the abstraction
 *	  in the presence of general system failures.
 *      - You must acquire the console spinlock thus restricting
 *	  the usability - what if the kernel fails with the spinlock
 *	  held - one still wishes to debug such situations.
 *      - How about debugging before the console(s) are registered?
 *      - None of the current consoles (sercons, vt_console_driver)
 *	  have read functions defined.
 *	- The standard pc keyboard and terminal drivers are interrupt
 *	  driven.   We cannot enable interrupts while kdb is active,
 *	  so the standard input functions cannot be used by kdb.
 *
 * An implementation could be improved by removing the need for
 * lock acquisition - just keep a 'struct console *kdbconsole;' global
 * variable which refers to the preferred kdb console.
 *
 * The bulk of this function is architecture dependent.
 *
 * The buffer size must be >= 2.  A buffer size of 2 means that the caller only
 * wants a single key.
 *
 * An escape key could be the start of a vt100 control sequence such as \e[D
 * (left arrow) or it could be a character in its own right.  The standard
 * method for detecting the difference is to wait for 2 seconds to see if there
 * are any other characters.  kdb is complicated by the lack of a timer service
 * (interrupts are off), by multiple input sources and by the need to sometimes
 * return after just one key.  Escape sequence processing has to be done as
 * states in the polling loop.
 */

char *
kdb_read(char *buffer, size_t bufsize)
{
	char *cp = buffer;
	char *bufend = buffer+bufsize-2;	/* Reserve space for newline and null byte */

	char *lastchar;
	char *p_tmp;
	char tmp;
	static char tmpbuffer[CMD_BUFLEN];
	int len = strlen(buffer);
	int len_tmp;
	int tab=0;
	int count;
	int i;
	int diag, dtab_count;

#define ESCAPE_UDELAY 1000
#define ESCAPE_DELAY 2*1000000/ESCAPE_UDELAY	/* 2 seconds worth of udelays */
	char escape_data[5];	/* longest vt100 escape sequence is 4 bytes */
	char *ped = escape_data;
	int escape_delay = 0;
	get_char_func *f, *f_escape = NULL;

	diag = kdbgetintenv("DTABCOUNT",&dtab_count);
	if (diag)
		dtab_count = 30;

	if (len > 0 ) {
		cp += len;
		if (*(buffer+len-1) == '\n')
			cp--;
	}

	lastchar = cp;
	*cp = '\0';
	kdb_printf("%s", buffer);

	for (;;) {
		int key;
		for (f = &poll_funcs[0]; ; ++f) {
			if (*f == NULL) {
				/* Reset NMI watchdog once per poll loop */
				touch_nmi_watchdog();
				f = &poll_funcs[0];
			}
			if (escape_delay == 2) {
				*ped = '\0';
				ped = escape_data;
				--escape_delay;
			}
			if (escape_delay == 1) {
				key = *ped++;
				if (!*ped)
					--escape_delay;
				break;
			}
			key = (*f)();
			if (key == -1) {
				if (escape_delay) {
					udelay(ESCAPE_UDELAY);
					--escape_delay;
				}
				continue;
			}
			if (bufsize <= 2) {
				if (key == '\r')
					key = '\n';
				kdb_printf("%c", key);
				*buffer++ = key;
				*buffer = '\0';
				return buffer;
			}
			if (escape_delay == 0 && key == '\e') {
				escape_delay = ESCAPE_DELAY;
				ped = escape_data;
				f_escape = f;
			}
			if (escape_delay) {
				*ped++ = key;
				if (f_escape != f) {
					escape_delay = 2;
					continue;
				}
				if (ped - escape_data == 1) {
					/* \e */
					continue;
				}
				else if (ped - escape_data == 2) {
					/* \e<something> */
					if (key != '[')
						escape_delay = 2;
					continue;
				} else if (ped - escape_data == 3) {
					/* \e[<something> */
					int mapkey = 0;
					switch (key) {
					case 'A': mapkey = 16; break;	/* \e[A, up arrow */
					case 'B': mapkey = 14; break;	/* \e[B, down arrow */
					case 'C': mapkey = 6; break;	/* \e[C, right arrow */
					case 'D': mapkey = 2; break;	/* \e[D, left arrow */
					case '1': /* dropthrough */
					case '3': /* dropthrough */
					case '4': mapkey = -1; break;	/* \e[<1,3,4>], may be home, del, end */
					}
					if (mapkey != -1) {
						if (mapkey > 0) {
							escape_data[0] = mapkey;
							escape_data[1] = '\0';
						}
						escape_delay = 2;
					}
					continue;
				} else if (ped - escape_data == 4) {
					/* \e[<1,3,4><something> */
					int mapkey = 0;
					if (key == '~') {
						switch (escape_data[2]) {
						case '1': mapkey = 1; break;	/* \e[1~, home */
						case '3': mapkey = 4; break;	/* \e[3~, del */
						case '4': mapkey = 5; break;	/* \e[4~, end */
						}
					}
					if (mapkey > 0) {
						escape_data[0] = mapkey;
						escape_data[1] = '\0';
					}
					escape_delay = 2;
					continue;
				}
			}
			break;	/* A key to process */
		}

		if (key != 9)
			tab = 0;
		switch (key) {
		case 8: /* backspace */
			if (cp > buffer) {
				if (cp < lastchar) {
					memcpy(tmpbuffer, cp, lastchar - cp);
					memcpy(cp-1, tmpbuffer, lastchar - cp);
				}
				*(--lastchar) = '\0';
				--cp;
				kdb_printf("\b%s \r", cp);
				tmp = *cp;
				*cp = '\0';
				kdb_printf(kdb_prompt_str);
				kdb_printf("%s", buffer);
				*cp = tmp;
			}
			break;
		case 13: /* enter */
			*lastchar++ = '\n';
			*lastchar++ = '\0';
			kdb_printf("\n");
			return buffer;
		case 4: /* Del */
			if(cp < lastchar) {
				memcpy(tmpbuffer, cp+1, lastchar - cp -1);
				memcpy(cp, tmpbuffer, lastchar - cp -1);
				*(--lastchar) = '\0';
				kdb_printf("%s \r", cp);
				tmp = *cp;
				*cp = '\0';
				kdb_printf(kdb_prompt_str);
				kdb_printf("%s", buffer);
				*cp = tmp;
			}
			break;
		case 1: /* Home */
			if(cp > buffer) {
				kdb_printf("\r");
				kdb_printf(kdb_prompt_str);
				cp = buffer;
			}
			break;
		case 5: /* End */
			if(cp < lastchar) {
				kdb_printf("%s", cp);
				cp = lastchar;
			}
			break;
		case 2: /* Left */
			if (cp > buffer) {
				kdb_printf("\b");
				--cp;
			}
			break;
		case 14: /* Down */
			memset(tmpbuffer, ' ', strlen(kdb_prompt_str)+(lastchar-buffer));
			*(tmpbuffer+strlen(kdb_prompt_str)+(lastchar-buffer)) = '\0';
			kdb_printf("\r%s\r", tmpbuffer);
			*lastchar = (char)key;
			*(lastchar+1) = '\0';
			return lastchar;
		case 6: /* Right */
			if (cp < lastchar) {
				kdb_printf("%c", *cp);
				++cp;
			}
			break;
		case 16: /* Up */
			memset(tmpbuffer, ' ', strlen(kdb_prompt_str)+(lastchar-buffer));
			*(tmpbuffer+strlen(kdb_prompt_str)+(lastchar-buffer)) = '\0';
			kdb_printf("\r%s\r", tmpbuffer);
			*lastchar = (char)key;
			*(lastchar+1) = '\0';
			return lastchar;
		case 9: /* Tab */
			if (tab < 2)
				++tab;
			p_tmp = buffer;
			while(*p_tmp==' ') p_tmp++;
			if (p_tmp<=cp) {
				memcpy(tmpbuffer, p_tmp, cp-p_tmp);
				*(tmpbuffer + (cp-p_tmp)) = '\0';
				p_tmp = strrchr(tmpbuffer, ' ');
				if (p_tmp)
					++p_tmp;
				else
					p_tmp = tmpbuffer;
				len = strlen(p_tmp);
				count = kallsyms_symbol_complete(p_tmp, sizeof(tmpbuffer) - (p_tmp - tmpbuffer));
				if (tab == 2) {
					if (count > 0) {
						kdb_printf("\n%d symbols are found.", count);
						if(count>dtab_count) {
							count=dtab_count;
							kdb_printf(" But only first %d symbols will be printed.\nYou can change the environment variable DTABCOUNT.", count);
						}
						kdb_printf("\n");
						for(i=0;i<count;i++) {
							if(kallsyms_symbol_next(p_tmp, i)<0)
								break;
							kdb_printf("%s ",p_tmp);
							*(p_tmp+len)='\0';
						}
						if(i>=dtab_count)kdb_printf("...");
						kdb_printf("\n");
						kdb_printf(kdb_prompt_str);
						kdb_printf("%s", buffer);
					}
				}
				else {
					if (count > 0) {
						len_tmp = strlen(p_tmp);
						strncpy(p_tmp+len_tmp,cp, lastchar-cp+1);
						len_tmp = strlen(p_tmp);
						strncpy(cp, p_tmp+len, len_tmp-len+1);
						len = len_tmp - len;
						kdb_printf("%s", cp);
						cp+=len;
						lastchar+=len;
					}
				}
				kdb_nextline = 1;		/* reset output line number */
			}
			break;
		default:
			if (key >= 32 &&lastchar < bufend) {
				if (cp < lastchar) {
					memcpy(tmpbuffer, cp, lastchar - cp);
					memcpy(cp+1, tmpbuffer, lastchar - cp);
					*++lastchar = '\0';
					*cp = key;
					kdb_printf("%s\r", cp);
					++cp;
					tmp = *cp;
					*cp = '\0';
					kdb_printf(kdb_prompt_str);
					kdb_printf("%s", buffer);
					*cp = tmp;
				} else {
					*++lastchar = '\0';
					*cp++ = key;
					kdb_printf("%c", key);
				}
			}
			break;
		}
	}
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
	if(prompt && kdb_prompt_str!=prompt)
		strncpy(kdb_prompt_str, prompt, CMD_BUFLEN);
	kdb_printf(kdb_prompt_str);
	kdb_nextline = 1;	/* Prompt and input resets line number */
	return kdb_read(buffer, bufsize);
}

/*
 * kdb_input_flush
 *
 *	Get rid of any buffered console input.
 *
 * Parameters:
 *	none
 * Returns:
 *	nothing
 * Locking:
 *	none
 * Remarks:
 *	Call this function whenever you want to flush input.  If there is any
 *	outstanding input, it ignores all characters until there has been no
 *	data for approximately half a second.
 */

#define FLUSH_UDELAY 100
#define FLUSH_DELAY 500000/FLUSH_UDELAY	/* 0.5 seconds worth of udelays */

static void
kdb_input_flush(void)
{
	get_char_func *f;
	int flush_delay = 1;
	while (flush_delay--) {
		touch_nmi_watchdog();
		for (f = &poll_funcs[0]; *f; ++f) {
			if ((*f)() != -1) {
				flush_delay = FLUSH_DELAY;
				break;
			}
		}
		if (flush_delay)
			udelay(FLUSH_UDELAY);
	}
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
	va_list ap;
	int diag;
	int linecount;
	int logging, saved_loglevel = 0;
	int do_longjmp = 0;
	int got_printf_lock = 0;
	struct console *c = console_drivers;
	static DEFINE_SPINLOCK(kdb_printf_lock);
	unsigned long uninitialized_var(flags);

	preempt_disable();
	/* Serialize kdb_printf if multiple cpus try to write at once.
	 * But if any cpu goes recursive in kdb, just print the output,
	 * even if it is interleaved with any other text.
	 */
	if (!KDB_STATE(PRINTF_LOCK)) {
		KDB_STATE_SET(PRINTF_LOCK);
		spin_lock_irqsave(&kdb_printf_lock, flags);
		got_printf_lock = 1;
		atomic_inc(&kdb_event);
	} else {
		__acquire(kdb_printf_lock);
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

#ifdef CONFIG_PPC64
	if (udbg_write)
		udbg_write(kdb_buffer, strlen(kdb_buffer));
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

	if (KDB_STATE(LONGJMP) && strchr(kdb_buffer, '\n'))
		kdb_nextline++;

	if (kdb_nextline == linecount) {
		char buf1[16]="";
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
			sprintf(buf2, moreprompt, get_cpu());
			put_cpu();
			moreprompt = buf2;
		}
#endif

		kdb_input_flush();
		c = console_drivers;
#ifdef CONFIG_SPARC64
		if (c == NULL)
			prom_printf("%s", moreprompt);
		else
#endif

#ifdef CONFIG_PPC64
		if (udbg_write)
			udbg_write(moreprompt, strlen(moreprompt));
		else
#endif

		while (c) {
			c->write(c, moreprompt, strlen(moreprompt));
			c = c->next;
		}

		if (logging)
			printk("%s", moreprompt);

		kdb_read(buf1, 2); /* '2' indicates to return immediately after getting one key. */
		kdb_nextline = 1;	/* Really set output line 1 */

		if ((buf1[0] == 'q') || (buf1[0] == 'Q')) {
			do_longjmp = 1;
			KDB_FLAG_SET(CMD_INTERRUPT);	/* command was interrupted */
			kdb_printf("\n");
		}
		else if (buf1[0] && buf1[0] != '\n') {
			kdb_printf("\nOnly 'q' or 'Q' are processed at more prompt, input ignored\n");
		}
		kdb_input_flush();
	}

	if (logging) {
		console_loglevel = saved_loglevel;
	}
	if (KDB_STATE(PRINTF_LOCK) && got_printf_lock) {
		got_printf_lock = 0;
		spin_unlock_irqrestore(&kdb_printf_lock, flags);
		KDB_STATE_CLEAR(PRINTF_LOCK);
		atomic_dec(&kdb_event);
	} else {
		__release(kdb_printf_lock);
	}
	preempt_enable();
	if (do_longjmp)
#ifdef kdba_setjmp
		kdba_longjmp(&kdbjmpbuf[smp_processor_id()], 1)
#endif	/* kdba_setjmp */
		;
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
 *	Select a console device.  Only use a VT console if the user specified
 *	or defaulted console= /^tty[0-9]*$/
 */

void __init
kdb_io_init(void)
{
	/*
	 * Select a console.
	 */
	struct console *c = console_drivers;
	int vt_console = 0;

	while (c) {
		if ((c->flags & CON_CONSDEV) && !kdbcons)
			kdbcons = c;
		if ((c->flags & CON_ENABLED) &&
		    strncmp(c->name, "tty", 3) == 0) {
			char *p = c->name + 3;
			while (isdigit(*p))
				++p;
			if (*p == '\0')
				vt_console = 1;
		}
		c = c->next;
	}

	if (kdbcons == NULL) {
		printk(KERN_ERR "kdb: Initialization failed - no console.  kdb is disabled.\n");
		KDB_FLAG_SET(NO_CONSOLE);
		kdb_on = 0;
	}
	if (!vt_console)
		KDB_FLAG_SET(NO_VT_CONSOLE);
	kdb_input_flush();
	return;
}

#ifdef CONFIG_KDB_USB

int kdb_no_usb = 0;

static int __init opt_kdbnousb(char *str)
{
	kdb_no_usb = 1;
	return 0;
}

early_param("kdbnousb", opt_kdbnousb);

#endif

EXPORT_SYMBOL(kdb_read);
