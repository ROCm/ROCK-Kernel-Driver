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
 * Define the direction that the stack grows
 */
#define KDB_STACK_DIRECTION	(-1)	/* Stack grows down */

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

#define KDB_HAVE_LONGJMP
#ifdef KDB_HAVE_LONGJMP
/*
 * Support for setjmp/longjmp
 */

/* __jmp_buf definition copied from libc/sysdeps/unix/sysv/linux/ia64/bits/setjmp.h */

#define _JBLEN  70

typedef struct __kdb_jmp_buf {
	unsigned long   __jmp_buf[_JBLEN];
} kdb_jmp_buf __attribute__ ((aligned (16)));

extern int kdba_setjmp(kdb_jmp_buf *);
extern int kdba_setjmp_asm(kdb_jmp_buf *);
extern void kdba_longjmp(kdb_jmp_buf *, int);
extern void kdba_longjmp_asm(kdb_jmp_buf *, int);

extern kdb_jmp_buf *kdbjmpbuf;
#endif	/* KDB_HAVE_LONGJMP */

/* Arch specific data saved for running processes */

struct kdba_running_process {
	struct switch_stack *sw;
};

static inline
void kdba_save_running(struct kdba_running_process *k, struct pt_regs *regs)
{
	/* ia64 data is saved by unw_init_running() via kdba_main_loop() for
	 * normal kdb entry and kdba_interrupt_switch_stack() for wierd
	 * interrupt handlers.
	 */
}

static inline
void kdba_unsave_running(struct kdba_running_process *k, struct pt_regs *regs)
{
}

/* kdba wrappers which want to save switch stack will call unw_init_running().
 * That routine only takes a void* so pack the interrupt data into a structure.
 */

#include <linux/interrupt.h>	/* for irqreturn_t */

struct kdba_sw_interrupt_data {
	irqreturn_t (*handler)(int, void *, struct pt_regs *);
	int irq;
	void *arg;
	struct pt_regs *regs;
	irqreturn_t ret;
};

extern void kdba_sw_interrupt(struct unw_frame_info *, void *);

/* Generate a wrapper function to create a switch_stack before
 * entering an interrupt handler.
 */

#define KDBA_SW_INTERRUPT_WRAPPER3(name,before,after)		\
irqreturn_t real_##name(int, void *, struct pt_regs *);		\
irqreturn_t name(int irq, void *arg, struct pt_regs *regs)	\
{								\
	struct kdba_sw_interrupt_data id = {			\
		.handler = &real_##name,			\
		.irq = irq,					\
		.arg = arg,					\
		.regs = regs,					\
		.ret = 0,					\
	};							\
	before;							\
	unw_init_running(kdba_sw_interrupt, &id);		\
	after;							\
	return id.ret;						\
}

#define KDBA_SW_INTERRUPT_WRAPPER(name)				\
	KDBA_SW_INTERRUPT_WRAPPER3(name,,)

/* The kdba handlers that sit between wrapper -> unw_init_running -> real
 * function are almost identical.  They differ in the function name, the
 * type of data passed as void* to unw_init_running, the value to print
 * in the debug statement and the invocation of the real function.
 *
 * data_type must be a structure that contains 'struct pt_regs *regs;'.
 */

#define KDBA_UNWIND_HANDLER(name, data_type, debug_value, invoke...)	\
void name(struct unw_frame_info *info, void *vdata)			\
{									\
	data_type *data = vdata;					\
	struct switch_stack *sw, *prev_sw;				\
	struct pt_regs *prev_regs;					\
	struct kdb_running_process *krp =				\
		kdb_running_process + smp_processor_id();		\
	KDB_DEBUG_STATE(__FUNCTION__, debug_value);			\
	prev_sw = krp->arch.sw;						\
	sw = (struct switch_stack *)(info+1);				\
	/* padding from unw_init_running */				\
	sw = (struct switch_stack *)(((unsigned long)sw + 15) & ~15);	\
	krp->arch.sw = sw;						\
	prev_regs = krp->regs;						\
	kdb_save_running(data->regs);					\
	invoke;								\
	kdb_unsave_running(data->regs);					\
	krp->regs = prev_regs;						\
	krp->arch.sw = prev_sw;						\
}

enum kdba_serial_console {
	KDBA_SC_NONE = 0,
	KDBA_SC_STANDARD,
	KDBA_SC_SGI_L1,
};

extern enum kdba_serial_console kdba_serial_console;

#endif	/* !_ASM_KDBPRIVATE_H */
