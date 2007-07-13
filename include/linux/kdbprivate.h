#ifndef _KDBPRIVATE_H
#define _KDBPRIVATE_H

/*
 * Kernel Debugger Architecture Independent Private Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */


#include <linux/dis-asm.h>
#include <asm/kdbprivate.h>
#include <asm/bfd.h>

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
	 * Internal debug flags
	 */
#define KDB_DEBUG_FLAG_BT	0x0001		/* Stack traceback debug */
#define KDB_DEBUG_FLAG_BP	0x0002		/* Breakpoint subsystem debug */
/*	KDB_DEBUG_FLAG_LBR	0x0004		WAS Print last branch register */
#define KDB_DEBUG_FLAG_AR	0x0008		/* Activation record, generic */
#define KDB_DEBUG_FLAG_ARA	0x0010		/* Activation record, arch specific */
/*      KDB_DEBUG_FLAG_CALLBACK	0x0020		WAS Event callbacks to kdb */
#define KDB_DEBUG_FLAG_STATE	0x0040		/* State flags */
#define KDB_DEBUG_FLAG_MASK	0xffff		/* All debug flags */
#define KDB_DEBUG_FLAG_SHIFT	16		/* Shift factor for dbflags */

#define KDB_DEBUG(flag)		(kdb_flags & (KDB_DEBUG_FLAG_##flag << KDB_DEBUG_FLAG_SHIFT))
#define KDB_DEBUG_STATE(text,value)	if (KDB_DEBUG(STATE)) kdb_print_state(text, value)

typedef enum {
	KDB_REPEAT_NONE = 0,		/* Do not repeat this command */
	KDB_REPEAT_NO_ARGS,		/* Repeat the command without arguments */
	KDB_REPEAT_WITH_ARGS,		/* Repeat the command including its arguments */
} kdb_repeat_t;

typedef int (*kdb_func_t)(int, const char **);

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
extern int kallsyms_symbol_next(char *prefix_name, int flag);
extern int kallsyms_symbol_complete(char *prefix_name, int max_len);

	/*
	 * Exported Symbols for kernel loadable modules to use.
	 */
extern int kdb_register(char *, kdb_func_t, char *, char *, short);
extern int kdb_register_repeat(char *, kdb_func_t, char *, char *, short, kdb_repeat_t);
extern int kdb_unregister(char *);

extern int kdb_getarea_size(void *, unsigned long, size_t);
extern int kdb_putarea_size(unsigned long, void *, size_t);

/* Like get_user and put_user, kdb_getarea and kdb_putarea take variable
 * names, not pointers.  The underlying *_size functions take pointers.
 */
#define kdb_getarea(x,addr) kdb_getarea_size(&(x), addr, sizeof((x)))
#define kdb_putarea(addr,x) kdb_putarea_size(addr, &(x), sizeof((x)))

extern int kdb_getphysword(unsigned long *word,
			unsigned long addr, size_t size);
extern int kdb_getword(unsigned long *, unsigned long, size_t);
extern int kdb_putword(unsigned long, unsigned long, size_t);

extern int kdbgetularg(const char *, unsigned long *);
extern char *kdbgetenv(const char *);
extern int kdbgetintenv(const char *, int *);
extern int kdbgetaddrarg(int, const char**, int*, unsigned long *,
			 long *, char **);
extern int kdbgetsymval(const char *, kdb_symtab_t *);
extern int kdbnearsym(unsigned long, kdb_symtab_t *);
extern void kdbnearsym_cleanup(void);
extern char *kdb_read(char *buffer, size_t bufsize);
extern char *kdb_strdup(const char *str, gfp_t type);
extern void kdb_symbol_print(kdb_machreg_t, const kdb_symtab_t *, unsigned int);

	 /*
	  * Do we have a set of registers?
	  */

#define KDB_NULL_REGS(regs) \
	(regs == (struct pt_regs *)NULL ? kdb_printf("%s: null regs - should never happen\n", __FUNCTION__), 1 : 0)

	 /*
	  * Routine for debugging the debugger state.
	  */

extern void kdb_print_state(const char *, int);

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
#define KDB_STATE_GO_SWITCH	0x00000800	/* go is switching back to initial cpu */
#define KDB_STATE_PRINTF_LOCK	0x00001000	/* Holds kdb_printf lock */
#define KDB_STATE_WAIT_IPI	0x00002000	/* Waiting for kdb_ipi() NMI */
#define KDB_STATE_RECURSE	0x00004000	/* Recursive entry to kdb */
#define KDB_STATE_IP_ADJUSTED	0x00008000	/* Restart IP has been adjusted */
#define KDB_STATE_GO1		0x00010000	/* go only releases one cpu */
#define KDB_STATE_KEYBOARD	0x00020000	/* kdb entered via keyboard on this cpu */
#define KDB_STATE_ARCH		0xff000000	/* Reserved for arch specific use */

#define KDB_STATE_CPU(flag,cpu)		(kdb_state[cpu] & KDB_STATE_##flag)
#define KDB_STATE_SET_CPU(flag,cpu)	((void)(kdb_state[cpu] |= KDB_STATE_##flag))
#define KDB_STATE_CLEAR_CPU(flag,cpu)	((void)(kdb_state[cpu] &= ~KDB_STATE_##flag))

#define KDB_STATE(flag)		KDB_STATE_CPU(flag,smp_processor_id())
#define KDB_STATE_SET(flag)	KDB_STATE_SET_CPU(flag,smp_processor_id())
#define KDB_STATE_CLEAR(flag)	KDB_STATE_CLEAR_CPU(flag,smp_processor_id())

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
extern kdb_bp_t kdb_breakpoints[/* KDB_MAXBPT */];

	/*
	 * Breakpoint architecture dependent functions.  Must be provided
	 * in some form for all architectures.
	 */
extern void kdba_initbp(void);
extern void kdba_printbp(kdb_bp_t *);
extern kdbhard_bp_t *kdba_allocbp(kdbhard_bp_t *, int *);
extern void kdba_freebp(kdbhard_bp_t *);
extern int kdba_parsebp(int, const char**, int *, kdb_bp_t*);
extern char *kdba_bptype(kdbhard_bp_t *);
extern void kdba_setsinglestep(struct pt_regs *);
extern void kdba_clearsinglestep(struct pt_regs *);

	/*
	 * Adjust instruction pointer architecture dependent function.  Must be
	 * provided in some form for all architectures.
	 */
extern void kdba_adjust_ip(kdb_reason_t, int, struct pt_regs *);

	/*
	 * KDB-only global function prototypes.
	 */
extern void kdb_id1(unsigned long);
extern void kdb_id_init(void);

	/*
	 * Initialization functions.
	 */
extern void kdba_init(void);
extern void kdb_io_init(void);

	/*
	 * Architecture specific function to read a string.
	 */
typedef int (*get_char_func)(void);
extern get_char_func poll_funcs[];

#ifndef	CONFIG_IA64
	/*
	 * Data for a single activation record on stack.
	 */

struct kdb_stack_info {
	kdb_machreg_t physical_start;
	kdb_machreg_t physical_end;
	kdb_machreg_t logical_start;
	kdb_machreg_t logical_end;
	kdb_machreg_t next;
	const char *  id;
};

struct kdb_activation_record {
	struct kdb_stack_info	stack;		/* information about current stack */
	kdb_machreg_t	start;			/* -> start of activation record */
	kdb_machreg_t	end;			/* -> end+1 of activation record */
	kdb_machreg_t	ret;			/* Return address to caller */
	kdb_machreg_t	oldfp;			/* Frame pointer for caller's frame */
	kdb_machreg_t	fp;			/* Frame pointer for callee's frame */
	int		args;			/* number of arguments detected */
	kdb_machreg_t	arg[KDBA_MAXARGS];	/* -> arguments */
};
#endif

	/*
	 * Architecture specific Stack Traceback functions.
	 */

struct task_struct;

extern int kdba_bt_address(kdb_machreg_t, int);
extern int kdba_bt_process(const struct task_struct *, int);

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

extern int kdb_id(int, const char **);
extern int kdb_bt(int, const char **);

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
extern kdb_machreg_t kdba_getpc(struct pt_regs *);

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
typedef unsigned long kdb_intstate_t;

extern void kdba_disableint(kdb_intstate_t *);
extern void kdba_restoreint(kdb_intstate_t *);

	/*
	 * SMP and process stack manipulation routines.
	 */
extern int kdba_ipi(struct pt_regs *, void (*)(void));
extern int kdba_main_loop(kdb_reason_t, kdb_reason_t, int, kdb_dbtrap_t, struct pt_regs *);
extern int kdb_main_loop(kdb_reason_t, kdb_reason_t, int, kdb_dbtrap_t, struct pt_regs *);

	/*
	 * General Disassembler interfaces
	 */
extern int kdb_dis_fprintf(PTR, const char *, ...) __attribute__ ((format (printf, 2, 3)));
extern int kdb_dis_fprintf_dummy(PTR, const char *, ...) __attribute__ ((format (printf, 2, 3)));
extern disassemble_info	kdb_di;

	/*
	 * Architecture Dependent Disassembler interfaces
	 */
extern int  kdba_id_printinsn(kdb_machreg_t, disassemble_info *);
extern int  kdba_id_parsemode(const char *, disassemble_info*);
extern void kdba_id_init(disassemble_info *);
extern void kdba_check_pc(kdb_machreg_t *);

	/*
	 * Miscellaneous functions and data areas
	 */
extern char *kdb_cmds[];
extern void kdb_syslog_data(char *syslog_data[]);
extern unsigned long kdb_task_state_string(const char *);
extern char kdb_task_state_char (const struct task_struct *);
extern unsigned long kdb_task_state(const struct task_struct *p, unsigned long mask);
extern void kdb_ps_suppressed(void);
extern void kdb_ps1(const struct task_struct *p);
extern int kdb_parse(const char *cmdstr);
extern void kdb_print_nameval(const char *name, unsigned long val);
extern void kdb_send_sig_info(struct task_struct *p, struct siginfo *info, int seqno);
#ifdef CONFIG_SWAP
extern void kdb_si_swapinfo(struct sysinfo *);
#else
#include <linux/swap.h>
#define kdb_si_swapinfo(x) si_swapinfo(x)
#endif
extern void kdb_meminfo_read_proc(void);
#ifdef	CONFIG_HUGETLB_PAGE
extern void kdb_hugetlb_report_meminfo(void);
#endif	/* CONFIG_HUGETLB_PAGE */
extern const char *kdb_walk_kallsyms(loff_t *pos);

	/*
	 * Architecture Dependant Local Processor setup & cleanup interfaces
	 */
extern void kdba_local_arch_setup(void);
extern void kdba_local_arch_cleanup(void);

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

/* Save data about running processes */

struct kdb_running_process {
	struct task_struct *p;
	struct pt_regs *regs;
	int seqno;				/* kdb sequence number */
	int irq_depth;				/* irq count */
	struct kdba_running_process arch;	/* arch dependent save data */
};

extern struct kdb_running_process kdb_running_process[/* NR_CPUS */];

extern void kdb_save_running(struct pt_regs *);
extern void kdb_unsave_running(struct pt_regs *);
extern struct task_struct *kdb_curr_task(int);

/* 	Incremented each time the main kdb loop is entered on the initial cpu,
 * 	it gives some indication of how old the saved data is.
 */
extern int kdb_seqno;

#define kdb_task_has_cpu(p) (task_curr(p))
extern void kdb_runqueue(unsigned long cpu, kdb_printf_t xxx_printf);

/* Simplify coexistence with NPTL */
#define	kdb_do_each_thread(g, p) do_each_thread(g, p)
#define	kdb_while_each_thread(g, p) while_each_thread(g, p)

#define GFP_KDB (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL)

extern void *debug_kmalloc(size_t size, gfp_t flags);
extern void debug_kfree(void *);
extern void debug_kusage(void);

extern void kdba_set_current_task(const struct task_struct *);
extern const struct task_struct *kdb_current_task;
extern struct pt_regs *kdb_current_regs;

/* Functions to safely read and write kernel areas.  The {to,from}_xxx
 * addresses are not necessarily valid, these functions must check for
 * validity.  If the arch already supports get and put routines with suitable
 * validation and/or recovery on invalid addresses then use those routines,
 * otherwise check it yourself.
 */

extern int kdba_putarea_size(unsigned long to_xxx, void *from, size_t size);
extern int kdba_getarea_size(void *to, unsigned long from_xxx, size_t size);
extern int kdba_verify_rw(unsigned long addr, size_t size);

#ifndef KDB_RUNNING_PROCESS_ORIGINAL
#define KDB_RUNNING_PROCESS_ORIGINAL kdb_running_process
#endif

extern int kdb_wait_for_cpus_secs;
extern void kdba_cpu_up(void);
extern char kdb_prompt_str[];

#endif	/* !_KDBPRIVATE_H */
