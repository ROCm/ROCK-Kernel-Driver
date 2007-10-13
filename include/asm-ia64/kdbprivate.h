#ifndef _ASM_KDBPRIVATE_H
#define _ASM_KDBPRIVATE_H

/*
 * Kernel Debugger Architecture Dependent Private Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

/* Definition of an machine instruction.
 * Takes care of VLIW processors like Itanium
 */

typedef struct {
	unsigned long inst[2];
} kdb_machinst_t;

/*
 * KDB_MAXBPT describes the total number of breakpoints
 * supported by this architecure.
 */
#define KDB_MAXBPT	16

/*
 * KDB_MAXHARDBPT describes the total number of hardware
 * breakpoint registers that exist.
 */
#define KDB_MAXHARDBPT	 4

/*
 * Platform specific environment entries
 */
#define KDB_PLATFORM_ENV	"IDMODE=ia64", "BYTESPERWORD=4", "IDCOUNT=8"

/*
 * Support for IA64 debug registers
 */
typedef struct _kdbhard_bp {
	kdb_machreg_t	bph_reg;	/* Register this breakpoint uses */

	unsigned int	bph_free:1;	/* Register available for use */
	unsigned int	bph_data:1;	/* Data Access breakpoint */

	unsigned int	bph_write:1;	/* Write Data breakpoint */
	unsigned int	bph_mode:2;	/* 0=inst, 1=write, 2=io, 3=read */
	unsigned int	bph_length:2;	/* 0=1, 1=2, 2=BAD, 3=4 (bytes) */
} kdbhard_bp_t;

#define getprsregs(regs)        ((struct switch_stack *)regs -1)

/* bkpt support using break inst instead of IBP reg */

/*
 * Define certain specific instructions
 */
#define BREAK_INSTR             (long)(KDB_BREAK_BREAK << (5+6))
#define INST_SLOT0_MASK         (0x1ffffffffffL << 5)

#define BKPTMODE_DATAR  3
#define BKPTMODE_IO     2
#define BKPTMODE_DATAW  1
#define BKPTMODE_INST   0

/* Some of the fault registers needed by kdb but not passed with
 * regs or switch stack.
 */
typedef struct fault_regs {
	unsigned long   isr ;
	unsigned long   ifa ;
	unsigned long   iim ;
	unsigned long   itir ;
} fault_regs_t ;

/*
 * Support for setjmp/longjmp
 */

/* __jmp_buf definition copied from libc/sysdeps/unix/sysv/linux/ia64/bits/setjmp.h */

#define _JBLEN  70

typedef struct __kdb_jmp_buf {
	unsigned long   __jmp_buf[_JBLEN];
} kdb_jmp_buf __attribute__ ((aligned (16)));

extern int kdba_setjmp(kdb_jmp_buf *);
extern void kdba_longjmp(kdb_jmp_buf *, int);
#define kdba_setjmp kdba_setjmp

extern kdb_jmp_buf *kdbjmpbuf;

/* Arch specific data saved for running processes */

struct kdba_running_process {
	struct switch_stack *sw;
};

extern void kdba_save_running(struct kdba_running_process *, struct pt_regs *);
extern void kdba_unsave_running(struct kdba_running_process *, struct pt_regs *);

/* kdba wrappers which want to save switch stack will call unw_init_running().
 * That routine only takes a void* so pack the interrupt data into a structure.
 */

#include <linux/interrupt.h>	/* for irqreturn_t */

enum kdba_serial_console {
	KDBA_SC_NONE = 0,
	KDBA_SC_STANDARD,
	KDBA_SC_SGI_L1,
};

extern enum kdba_serial_console kdba_serial_console;

#define KDB_RUNNING_PROCESS_ORIGINAL kdb_running_process_save
extern struct kdb_running_process *kdb_running_process_save; /* [NR_CPUS] */

extern void kdba_wait_for_cpus(void);

#endif	/* !_ASM_KDBPRIVATE_H */
