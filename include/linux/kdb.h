#ifndef _KDB_H
#define _KDB_H

/*
 * Kernel Debugger Architecture Independent Global Headers
 *
 * Copyright (C) 1999-2002 Silicon Graphics, Inc.  All Rights Reserved
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
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
#include <asm/kdb.h>

#define KDB_MAJOR_VERSION	2
#define KDB_MINOR_VERSION	4
#define KDB_TEST_VERSION	" ppc64-03.11.2003"

	/*
	 * kdb_initial_cpu is initialized to -1, and is set to the cpu
	 * number whenever the kernel debugger is entered.
	 */
extern volatile int kdb_initial_cpu;	
#ifdef	CONFIG_KDB
#define KDB_IS_RUNNING() (kdb_initial_cpu != -1)
#else
#define KDB_IS_RUNNING() (0)
#endif	/* CONFIG_KDB */

	/*
	 * kdb_on
	 *
	 * 	Defines whether kdb is on or not.  Default value
	 *	is set by CONFIG_KDB_OFF.  Boot with kdb=on/off
	 *	or echo "[01]" > /proc/sys/kernel/kdb to change it.
	 */
extern int kdb_on;

	/*
	 * kdb_serial.iobase is initialized to zero, and is set to the I/O
	 * address of the serial port when the console is setup in
	 * serial_console_setup.
	 */
extern struct kdb_serial {
	int io_type;
	unsigned long iobase;
	unsigned long ioreg_shift;
} kdb_serial;

	/*
	 * kdb_diemsg
	 *
	 *	Contains a pointer to the last string supplied to the
	 *	kernel 'die' panic function.
	 */
extern const char *kdb_diemsg;

	/*
	 * KDB_FLAG_EARLYKDB is set when the 'kdb' option is specified
	 * as a boot parameter (e.g. via lilo).   It indicates that the
	 * kernel debugger should be entered as soon as practical.
	 */
#define KDB_FLAG_EARLYKDB	0x00000001

	/*
	 * Internal debug flags
	 */
#define KDB_DEBUG_FLAG_BT	0x0001		/* Stack traceback debug */
#define KDB_DEBUG_FLAG_BP	0x0002		/* Breakpoint subsystem debug */
#define KDB_DEBUG_FLAG_LBR	0x0004		/* Print last branch register */
#define KDB_DEBUG_FLAG_AR	0x0008		/* Activation record, generic */
#define KDB_DEBUG_FLAG_ARA	0x0010		/* Activation record, arch specific */
/*      KDB_DEBUG_FLAG_CALLBACK	0x0020		WAS Event callbacks to kdb */
#define KDB_DEBUG_FLAG_STATE	0x0040		/* State flags */
#define KDB_DEBUG_FLAG_MASK	0xffff		/* All debug flags */
#define KDB_DEBUG_FLAG_SHIFT	16		/* Shift factor for dbflags */

extern volatile int kdb_flags;			/* Global flags, see kdb_state for per cpu state */

#define KDB_FLAG(flag)		(kdb_flags & KDB_FLAG_##flag)
#define KDB_FLAG_SET(flag)	((void)(kdb_flags |= KDB_FLAG_##flag))
#define KDB_FLAG_CLEAR(flag)	((void)(kdb_flags &= ~KDB_FLAG_##flag))
#define KDB_DEBUG(flag)		(kdb_flags & (KDB_DEBUG_FLAG_##flag << KDB_DEBUG_FLAG_SHIFT))
#define KDB_DEBUG_STATE(text,value)	if (KDB_DEBUG(STATE)) kdb_print_state(text, value)

	/*
	 * Per cpu kdb state.  A cpu can be under kdb control but outside kdb,
	 * for example when doing single step.
	 */
volatile extern int kdb_state[ /*NR_CPUS*/ ];
#define KDB_STATE_KDB		0x00000001	/* Cpu is inside kdb */
#define KDB_STATE_LEAVING	0x00000002	/* Cpu is leaving kdb */
#define KDB_STATE_CMD		0x00000004	/* Running a kdb command */
#define KDB_STATE_KDB_CONTROL	0x00000008	/* This cpu is under kdb control */
#define KDB_STATE_HOLD_CPU	0x00000010	/* Hold this cpu inside kdb */
#define KDB_STATE_DOING_SS	0x00000020	/* Doing ss command */
#define KDB_STATE_DOING_SSB	0x00000040	/* Doing ssb command, DOING_SS is also set */
#define KDB_STATE_SSBPT		0x00000080	/* Install breakpoint after one ss, independent of DOING_SS */
#define KDB_STATE_REENTRY	0x00000100	/* Valid re-entry into kdb */
#define KDB_STATE_SUPPRESS	0x00000200	/* Suppress error messages */
#define KDB_STATE_LONGJMP	0x00000400	/* longjmp() data is available */
 /* Spare, was    NO_WATCHDOG	0x00000800 */
#define KDB_STATE_PRINTF_LOCK	0x00001000	/* Holds kdb_printf lock */
#define KDB_STATE_WAIT_IPI	0x00002000	/* Waiting for kdb_ipi() NMI */
#define KDB_STATE_RECURSE	0x00004000	/* Recursive entry to kdb */
#define KDB_STATE_IP_ADJUSTED	0x00008000	/* Restart IP has been adjusted */
#define KDB_STATE_NO_BP_DELAY	0x00010000	/* No need to delay breakpoints */
#define KDB_STATE_ARCH		0xff000000	/* Reserved for arch specific use */

#define KDB_STATE_CPU(flag,cpu)		(kdb_state[cpu] & KDB_STATE_##flag)
#define KDB_STATE_SET_CPU(flag,cpu)	((void)(kdb_state[cpu] |= KDB_STATE_##flag))
#define KDB_STATE_CLEAR_CPU(flag,cpu)	((void)(kdb_state[cpu] &= ~KDB_STATE_##flag))

#define KDB_STATE(flag)		KDB_STATE_CPU(flag,smp_processor_id())
#define KDB_STATE_SET(flag)	KDB_STATE_SET_CPU(flag,smp_processor_id())
#define KDB_STATE_CLEAR(flag)	KDB_STATE_CLEAR_CPU(flag,smp_processor_id())

	/*
	 * External entry point for the kernel debugger.  The pt_regs
	 * at the time of entry are supplied along with the reason for
	 * entry to the kernel debugger.
	 */

typedef enum {
	KDB_REASON_CALL = 1,		/* Call kdb() directly - regs should be valid */
	KDB_REASON_FAULT,		/* Kernel fault - regs valid */
	KDB_REASON_BREAK,		/* Breakpoint inst. - regs valid */
	KDB_REASON_DEBUG,		/* Debug Fault - regs valid */
	KDB_REASON_OOPS,		/* Kernel Oops - regs valid */
	KDB_REASON_SWITCH,		/* CPU switch - regs valid*/
	KDB_REASON_ENTER,		/* KDB_ENTER() trap/fault - regs valid */
	KDB_REASON_KEYBOARD,		/* Keyboard entry - regs valid */
	KDB_REASON_NMI,			/* Non-maskable interrupt; regs valid */
	KDB_REASON_WATCHDOG,		/* Watchdog interrupt; regs valid */
	KDB_REASON_RECURSE,		/* Recursive entry to kdb; regs probably valid */
	KDB_REASON_SILENT,		/* Silent entry/exit to kdb; regs invalid */
	KDB_REASON_RESET,		/* Reset vector, for all runner-up cpus; regs valid */
} kdb_reason_t;

typedef enum {
	KDB_REPEAT_NONE = 0,		/* Do not repeat this command */
	KDB_REPEAT_NO_ARGS,		/* Repeat the command without arguments */
	KDB_REPEAT_WITH_ARGS,		/* Repeat the command including its arguments */
} kdb_repeat_t;

#ifdef	CONFIG_KDB
extern int   kdb(kdb_reason_t, int, struct pt_regs *);
#else
#define kdb(reason,error_code,frame) (0)
#endif

typedef int (*kdb_func_t)(int, const char **, const char **, struct pt_regs *);

	/*
	 * Symbol table format returned by kallsyms.
	 */

typedef struct __ksymtab {
		unsigned long value;		/* Address of symbol */
		const char *mod_name;		/* Module containing symbol or "kernel" */
		unsigned long mod_start;
		unsigned long mod_end;
		const char *sec_name;		/* Section containing symbol */
		unsigned long sec_start;
		unsigned long sec_end;
		const char *sym_name;		/* Full symbol name, including any version */
		unsigned long sym_start;
		unsigned long sym_end;
		} kdb_symtab_t;

	/*
	 * Exported Symbols for kernel loadable modules to use.
	 */
extern int           kdb_register(char *, kdb_func_t, char *, char *, short);
extern int           kdb_register_repeat(char *, kdb_func_t, char *, char *, short, kdb_repeat_t);
extern int           kdb_unregister(char *);

extern int	     kdb_getarea_size(void *, unsigned long, size_t);
extern int	     kdb_putarea_size(unsigned long, void *, size_t);

/* Like get_user and put_user, kdb_getarea and kdb_putarea take variable
 * names, not pointers.  The underlying *_size functions take pointers.
 */
#define kdb_getarea(x,addr)	kdb_getarea_size(&(x), addr, sizeof((x)))
#define kdb_putarea(addr,x)	kdb_putarea_size(addr, &(x), sizeof((x)))

extern int	     kdb_getword(unsigned long *, unsigned long, size_t);
extern int	     kdb_putword(unsigned long, unsigned long, size_t);

extern int	     kdbgetularg(const char *, unsigned long *);
extern char         *kdbgetenv(const char *);
extern int	     kdbgetintenv(const char *, int *);
extern int	     kdbgetaddrarg(int, const char**, int*, unsigned long *,
			           long *, char **, struct pt_regs *);
extern int	     kdbgetsymval(const char *, kdb_symtab_t *);
extern int	     kdbnearsym(unsigned long, kdb_symtab_t *);
extern void	     kdb_printf(const char *,...)
		     __attribute__ ((format (printf, 1, 2)));
extern void	     kdb_init(void);
extern void	     kdb_symbol_print(kdb_machreg_t, const kdb_symtab_t *, unsigned int);
extern char	    *kdb_read(char *buffer, size_t bufsize);
extern char	    *kdb_strdup(const char *str, int type);

#if defined(CONFIG_SMP)
	/*
	 * Kernel debugger non-maskable IPI handler.
	 */
extern int           kdb_ipi(struct pt_regs *, void (*ack_interrupt)(void));
extern void	     smp_kdb_stop(void);
#else	/* CONFIG_SMP */
#define	smp_kdb_stop()
#endif	/* CONFIG_SMP */

	/*
	 * Interface from general kernel to enable any hardware
	 * error reporting mechanisms.  Such as the Intel Machine
	 * Check Architecture, for example.
	 */
extern void	     kdb_enablehwfault(void);

	 /*
	  * Do we have a set of registers?
	  */

#define KDB_NULL_REGS(regs) \
	(regs == (struct pt_regs *)NULL ? kdb_printf("%s: null regs - should never happen\n", __FUNCTION__), 1 : 0)

	 /*
	  * Routine for debugging the debugger state.
	  */

extern void kdb_print_state(const char *, int);

#ifdef CONFIG_KDB_USB
#include <linux/usb.h>
#define KDB_USB_ACTIVE 	1 /* Keyboard driver is usbkbd */
#define HID_ACTIVE 	2 /* Keyboard driver is hid    */

struct kdb_usb_exchange {
	void *uhci;			/* pointer to the UHCI structure */
  	struct urb *urb;		/* pointer to the URB */
	unsigned char *buffer;		/* pointer to buffer */
	void (*poll_func)(void *, struct urb *); /* pointer to the polling function */
	void (*reset_timer)(void);	/* pointer to the reset timer function */
	int driver;			/* driver mode, see above KDB_USB_KBD */
};
extern struct kdb_usb_exchange kdb_usb_infos; /* KDB common structure */
#endif /* CONFIG_KDB_USB */

#endif	/* !_KDB_H */
