#ifndef _KDBPRIVATE_H
#define _KDBPRIVATE_H

/*
 * Kernel Debugger Architecture Independent Private Headers
 *
 * Copyright (C) 1999-2002 Silicon Graphics, Inc.  All Rights Reserved
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

#include <linux/dis-asm.h>
#include <asm/kdbprivate.h>

#include "bfd.h"

/*
 * Kernel Debugger Error codes.  Must not overlap with command codes.
 */

#define KDB_NOTFOUND	(-1)
#define KDB_ARGCOUNT	(-2)
#define KDB_BADWIDTH	(-3)
#define KDB_BADRADIX	(-4)
#define KDB_NOTENV	(-5)
#define KDB_NOENVVALUE	(-6)
#define KDB_NOTIMP	(-7)
#define KDB_ENVFULL	(-8)
#define KDB_ENVBUFFULL	(-9 )
#define KDB_TOOMANYBPT	(-10)
#define KDB_TOOMANYDBREGS (-11)
#define KDB_DUPBPT	(-12)
#define KDB_BPTNOTFOUND	(-13)
#define KDB_BADMODE	(-14)
#define KDB_BADINT	(-15)
#define KDB_INVADDRFMT  (-16)
#define KDB_BADREG      (-17)
#define KDB_BADCPUNUM   (-18)
#define KDB_BADLENGTH	(-19)
#define KDB_NOBP	(-20)
#define KDB_BADADDR	(-21)

/*
 * Kernel Debugger Command codes.  Must not overlap with error codes.
 */
#define KDB_CMD_GO	(-1001)
#define KDB_CMD_CPU	(-1002)
#define KDB_CMD_SS	(-1003)
#define KDB_CMD_SSB	(-1004)

	/*
	 * kdb_nextline
	 *
	 * 	Contains the current line number on the screen.  Used
	 *	to handle the built-in pager (LINES env variable)
	 */
extern volatile int kdb_nextline;

	/*
	 * Breakpoint state
	 *
	 * 	Each active and inactive breakpoint is represented by
	 * 	an instance of the following data structure.
	 */

typedef struct _kdb_bp {
	bfd_vma 	bp_addr;	/* Address breakpoint is present at */
	kdb_machinst_t	bp_inst;	/* Replaced instruction */

	unsigned int	bp_free:1;	/* This entry is available */

	unsigned int	bp_enabled:1;	/* Breakpoint is active in register */
	unsigned int	bp_global:1;	/* Global to all processors */

	unsigned int	bp_hardtype:1;	/* Uses hardware register */
	unsigned int	bp_forcehw:1;	/* Force hardware register */
	unsigned int	bp_installed:1;	/* Breakpoint is installed */
	unsigned int	bp_delay:1;	/* Do delayed bp handling */
	unsigned int	bp_delayed:1;	/* Delayed breakpoint */

	int		bp_cpu;		/* Cpu #  (if bp_global == 0) */
	kdbhard_bp_t	bp_template;	/* Hardware breakpoint template */
	kdbhard_bp_t	*bp_hard;	/* Hardware breakpoint structure */
	int		bp_adjust;	/* Adjustment to PC for real instruction */
} kdb_bp_t;

	/*
	 * Breakpoint handling subsystem global variables
	 */
extern kdb_bp_t		kdb_breakpoints[/* KDB_MAXBPT */];

	/*
	 * Breakpoint architecture dependent functions.  Must be provided
	 * in some form for all architectures.
	 */
extern void 		kdba_initbp(void);
extern void		kdba_printbp(kdb_bp_t *);
extern void		kdba_printbpreg(kdbhard_bp_t *);
extern kdbhard_bp_t	*kdba_allocbp(kdbhard_bp_t *, int *);
extern void		kdba_freebp(kdbhard_bp_t *);
extern int		kdba_parsebp(int, const char**, int *, kdb_bp_t*);
extern char 		*kdba_bptype(kdbhard_bp_t *);
extern void		kdba_setsinglestep(struct pt_regs *);
extern void		kdba_clearsinglestep(struct pt_regs *);

	/*
	 * Adjust instruction pointer architecture dependent function.  Must be
	 * provided in some form for all architectures.
	 */
extern void		kdba_adjust_ip(kdb_reason_t, int, struct pt_regs *);

	/*
	 * KDB-only global function prototypes.
	 */
extern void	     kdb_id1(unsigned long);
extern void	     kdb_id_init(void);

	/*
	 * Architecture dependent function to enable any
	 * processor machine check exception handling modes.
	 */
extern void	     kdba_enable_mce(void);

extern void	     kdba_enable_lbr(void);
extern void	     kdba_disable_lbr(void);
extern void	     kdba_print_lbr(void);

	/*
	 * Initialization functions.
	 */
extern void	     kdba_init(void);
extern void	     kdb_io_init(void);

	/*
	 * Architecture specific function to read a string.
	 */
extern char *	     kdba_read(char *, size_t);

	/*
	 * Data for a single activation record on stack.
	 */

typedef struct __kdb_activation_record {
	kdb_machreg_t	start;		/* -> start of activation record */
	kdb_machreg_t	end;		/* -> end+1 of activation record */
	kdb_machreg_t	ret;		/* Return address to caller */
	kdb_machreg_t	oldfp;		/* Frame pointer for caller's frame */
	kdb_machreg_t	fp;		/* Frame pointer for callee's frame */
	kdb_machreg_t	arg0;		/* -> First argument on stack (in previous ar) */
	unsigned long	locals;		/* Bytes allocated for local variables */
	unsigned long	regs;		/* Bytes allocated for saved registers */
	unsigned long	args;		/* Bytes allocated for arguments (in previous ar) */
	unsigned long	setup;		/* Bytes allocated for setup data */
} kdb_ar_t;

	/*
	 * General Stack Traceback functions.
	 */

extern int	     kdb_get_next_ar(kdb_machreg_t, kdb_machreg_t,
				     kdb_machreg_t, kdb_machreg_t,
				     kdb_machreg_t,
				     kdb_ar_t *, kdb_symtab_t *);

	/*
	 * Architecture specific Stack Traceback functions.
	 */

struct task_struct;

extern int	     kdba_bt_stack(struct pt_regs *, kdb_machreg_t *,
				   int, struct task_struct *);
extern int	     kdba_bt_process(struct task_struct *, int);
extern int	     kdba_prologue(const kdb_symtab_t *, kdb_machreg_t,
				   kdb_machreg_t, kdb_machreg_t, kdb_machreg_t,
				   int, kdb_ar_t *);
	/*
	 * KDB Command Table
	 */

typedef struct _kdbtab {
        char    *cmd_name;		/* Command name */
        kdb_func_t cmd_func;		/* Function to execute command */
        char    *cmd_usage;		/* Usage String for this command */
        char    *cmd_help;		/* Help message for this command */
        short    cmd_flags;		/* Parsing flags */
        short    cmd_minlen;		/* Minimum legal # command chars required */
	kdb_repeat_t cmd_repeat;	/* Does command auto repeat on enter? */
} kdbtab_t;

	/*
	 * External command function declarations
	 */

extern int kdb_id(int, const char **, const char **, struct pt_regs *);
extern int kdb_bp(int, const char **, const char **, struct pt_regs *);
extern int kdb_bc(int, const char **, const char **, struct pt_regs *);
extern int kdb_bt(int, const char **, const char **, struct pt_regs *);
extern int kdb_ss(int, const char **, const char **, struct pt_regs *);

	/*
	 * External utility function declarations
	 */
extern char* kdb_getstr(char *, size_t, char *);

	/*
	 * Register contents manipulation
	 */
extern int kdba_getregcontents(const char *, struct pt_regs *, kdb_machreg_t *);
extern int kdba_setregcontents(const char *, struct pt_regs *, kdb_machreg_t);
extern int kdba_dumpregs(struct pt_regs *, const char *, const char *);
extern int kdba_setpc(struct pt_regs *, kdb_machreg_t);
extern kdb_machreg_t   kdba_getpc(struct pt_regs *);

	/*
	 * Debug register handling.
	 */
extern void kdba_installdbreg(kdb_bp_t*);
extern void kdba_removedbreg(kdb_bp_t*);

	/*
	 * Breakpoint handling - External interfaces
	 */
extern void kdb_initbptab(void);
extern void kdb_bp_install_global(struct pt_regs *);
extern void kdb_bp_install_local(struct pt_regs *);
extern void kdb_bp_remove_global(void);
extern void kdb_bp_remove_local(void);

	/*
	 * Breakpoint handling - Internal to kdb_bp.c/kdba_bp.c
	 */
extern int kdba_installbp(struct pt_regs *regs, kdb_bp_t *);
extern int kdba_removebp(kdb_bp_t *);


typedef enum {
	KDB_DB_BPT,	/* Breakpoint */
	KDB_DB_SS,	/* Single-step trap */
	KDB_DB_SSB,	/* Single step to branch */
	KDB_DB_SSBPT,	/* Single step over breakpoint */
	KDB_DB_NOBPT	/* Spurious breakpoint */
} kdb_dbtrap_t;

extern kdb_dbtrap_t kdba_db_trap(struct pt_regs *, int);	/* DEBUG trap/fault handler */
extern kdb_dbtrap_t kdba_bp_trap(struct pt_regs *, int);	/* Breakpoint trap/fault hdlr */

	/*
	 * Interrupt Handling
	 */
typedef int kdb_intstate_t;

extern void kdba_disableint(kdb_intstate_t *);
extern void kdba_restoreint(kdb_intstate_t *);

	/*
	 * SMP and process stack manipulation routines.
	 */
extern int	     kdba_ipi(struct pt_regs *, void (*)(void));
extern int	     kdba_main_loop(kdb_reason_t, kdb_reason_t, int, kdb_dbtrap_t, struct pt_regs *);
extern int           kdb_main_loop(kdb_reason_t, kdb_reason_t, int, kdb_dbtrap_t, struct pt_regs *);

	/*
	 * General Disassembler interfaces
	 */
extern int kdb_dis_fprintf(PTR, const char *, ...) __attribute__ ((format (printf, 2, 3)));
extern int kdb_dis_fprintf_dummy(PTR, const char *, ...) __attribute__ ((format (printf, 2, 3)));
extern disassemble_info	kdb_di;

	/*
	 * Architecture Dependent Disassembler interfaces
	 */
extern void kdba_printaddress(kdb_machreg_t, disassemble_info *, int);
extern int  kdba_id_printinsn(kdb_machreg_t, disassemble_info *);
extern int  kdba_id_parsemode(const char *, disassemble_info*);
extern void kdba_id_init(disassemble_info *);
extern void kdba_check_pc(kdb_machreg_t *);

	/*
	 * Miscellaneous functions and data areas
	 */
extern char *kdb_cmds[];
extern void kdb_syslog_data(char *syslog_data[]);
extern unsigned long kdb_task_state_string(int argc, const char **argv, const char **envp);
extern unsigned long kdb_task_state(const struct task_struct *p, unsigned long mask);
extern void kdb_ps1(struct task_struct *p);

	/*
	 * Defines for kdb_symbol_print.
	 */
#define KDB_SP_SPACEB	0x0001		/* Space before string */
#define KDB_SP_SPACEA	0x0002		/* Space after string */
#define KDB_SP_PAREN	0x0004		/* Parenthesis around string */
#define KDB_SP_VALUE	0x0008		/* Print the value of the address */
#define KDB_SP_SYMSIZE	0x0010		/* Print the size of the symbol */
#define KDB_SP_NEWLINE	0x0020		/* Newline after string */
#define KDB_SP_DEFAULT (KDB_SP_VALUE|KDB_SP_PAREN)

#endif	/* !_KDBPRIVATE_H */
