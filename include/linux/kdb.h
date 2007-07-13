#ifndef _KDB_H
#define _KDB_H

/*
 * Kernel Debugger Architecture Independent Global Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2007 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <asm/atomic.h>

#ifdef CONFIG_KDB
/* These are really private, but they must be defined before including
 * asm-$(ARCH)/kdb.h, so make them public and put them here.
 */
extern int kdb_getuserarea_size(void *, unsigned long, size_t);
extern int kdb_putuserarea_size(unsigned long, void *, size_t);

#include <asm/kdb.h>
#endif

#define KDB_MAJOR_VERSION	4
#define KDB_MINOR_VERSION	4
#define KDB_TEST_VERSION	""

/*
 * kdb_initial_cpu is initialized to -1, and is set to the cpu
 * number whenever the kernel debugger is entered.
 */
extern volatile int kdb_initial_cpu;
extern atomic_t kdb_event;
extern atomic_t kdb_8250;
#ifdef	CONFIG_KDB
#define KDB_IS_RUNNING() (kdb_initial_cpu != -1)
#define KDB_8250() (atomic_read(&kdb_8250) != 0)
#else
#define KDB_IS_RUNNING() (0)
#define KDB_8250() (0)
#endif	/* CONFIG_KDB */

/*
 * kdb_on
 *
 * 	Defines whether kdb is on or not.  Default value
 *	is set by CONFIG_KDB_OFF.  Boot with kdb=on/off/on-nokey
 *	or echo "[012]" > /proc/sys/kernel/kdb to change it.
 */
extern int kdb_on;

#if defined(CONFIG_SERIAL_8250_CONSOLE) || defined(CONFIG_SERIAL_SGI_L1_CONSOLE)
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
#endif

/*
 * kdb_diemsg
 *
 *	Contains a pointer to the last string supplied to the
 *	kernel 'die' panic function.
 */
extern const char *kdb_diemsg;

#define KDB_FLAG_EARLYKDB	(1 << 0)	/* set from boot parameter kdb=early */
#define KDB_FLAG_CATASTROPHIC	(1 << 1)	/* A catastrophic event has occurred */
#define KDB_FLAG_CMD_INTERRUPT	(1 << 2)	/* Previous command was interrupted */
#define KDB_FLAG_NOIPI		(1 << 3)	/* Do not send IPIs */
#define KDB_FLAG_ONLY_DO_DUMP	(1 << 4)	/* Only do a dump, used when kdb is off */
#define KDB_FLAG_NO_CONSOLE	(1 << 5)	/* No console is available, kdb is disabled */
#define KDB_FLAG_NO_VT_CONSOLE	(1 << 6)	/* No VT console is available, do not use keyboard */
#define KDB_FLAG_NO_I8042	(1 << 7)	/* No i8042 chip is available, do not use keyboard */
#define KDB_FLAG_RECOVERY	(1 << 8)	/* kdb is being entered for an error which has been recovered */

extern volatile int kdb_flags;			/* Global flags, see kdb_state for per cpu state */

extern void kdb_save_flags(void);
extern void kdb_restore_flags(void);

#define KDB_FLAG(flag)		(kdb_flags & KDB_FLAG_##flag)
#define KDB_FLAG_SET(flag)	((void)(kdb_flags |= KDB_FLAG_##flag))
#define KDB_FLAG_CLEAR(flag)	((void)(kdb_flags &= ~KDB_FLAG_##flag))

/*
 * External entry point for the kernel debugger.  The pt_regs
 * at the time of entry are supplied along with the reason for
 * entry to the kernel debugger.
 */

typedef enum {
	KDB_REASON_ENTER=1,		/* KDB_ENTER() trap/fault - regs valid */
	KDB_REASON_ENTER_SLAVE,		/* KDB_ENTER_SLAVE() trap/fault - regs valid */
	KDB_REASON_BREAK,		/* Breakpoint inst. - regs valid */
	KDB_REASON_DEBUG,		/* Debug Fault - regs valid */
	KDB_REASON_OOPS,		/* Kernel Oops - regs valid */
	KDB_REASON_SWITCH,		/* CPU switch - regs valid*/
	KDB_REASON_KEYBOARD,		/* Keyboard entry - regs valid */
	KDB_REASON_NMI,			/* Non-maskable interrupt; regs valid */
	KDB_REASON_RECURSE,		/* Recursive entry to kdb; regs probably valid */
	KDB_REASON_CPU_UP,		/* Add one cpu to kdb; regs invalid */
	KDB_REASON_SILENT,		/* Silent entry/exit to kdb; regs invalid - internal only */
} kdb_reason_t;

#ifdef	CONFIG_KDB
extern fastcall int kdb(kdb_reason_t, int, struct pt_regs *);
#else
#define kdb(reason,error_code,frame) (0)
#endif

/* Mainly used by kdb code, but this function is sometimes used
 * by hacked debug code so make it generally available, not private.
 */
extern void kdb_printf(const char *,...)
	    __attribute__ ((format (printf, 1, 2)));
typedef void (*kdb_printf_t)(const char *, ...)
	     __attribute__ ((format (printf, 1, 2)));
extern void kdb_init(void);

#if defined(CONFIG_SMP)
/*
 * Kernel debugger non-maskable IPI handler.
 */
extern int kdb_ipi(struct pt_regs *, void (*ack_interrupt)(void));
extern void smp_kdb_stop(void);
#else	/* CONFIG_SMP */
#define	smp_kdb_stop()
#endif	/* CONFIG_SMP */

#ifdef CONFIG_KDB_USB
#include <linux/usb.h>

struct kdb_usb_exchange {
	void *uhci;			/* pointer to the UHCI structure */
	struct urb *urb;		/* pointer to the URB */
	unsigned char *buffer;		/* pointer to buffer */
	void (*poll_func)(void *, struct urb *); /* pointer to the polling function */
	void (*reset_timer)(void);	/* pointer to the reset timer function */
};
extern struct kdb_usb_exchange kdb_usb_infos; /* KDB common structure */
#endif /* CONFIG_KDB_USB */

static inline
int kdb_process_cpu(const struct task_struct *p)
{
	unsigned int cpu = task_thread_info(p)->cpu;
	if (cpu > NR_CPUS)
		cpu = 0;
	return cpu;
}

extern const char kdb_serial_str[];

#endif	/* !_KDB_H */
