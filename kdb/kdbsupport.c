/*
 * Kernel Debugger Architecture Independent Support Functions
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

#include <stdarg.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kallsyms.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>

#include <linux/kdb.h>
#include <linux/kdbprivate.h>

/*
 * Symbol table functions.
 */

/*
 * kdbgetsymval
 *
 *	Return the address of the given symbol.
 *
 * Parameters:
 * 	symname	Character string containing symbol name
 *      symtab  Structure to receive results
 * Outputs:
 * Returns:
 *	0	Symbol not found, symtab zero filled
 *	1	Symbol mapped to module/symbol/section, data in symtab
 * Locking:
 *	None.
 * Remarks:
 */

int
kdbgetsymval(const char *symname, kdb_symtab_t *symtab)
{
	unsigned long symsize;
	memset(symtab, 0, sizeof(*symtab));

	symtab->sym_name = symname;
	symtab->sym_start = kallsyms_get_addr((char *)symname,&symsize);
	symtab->sym_end=symtab->sym_start+symsize;
	if (symtab->sym_start) {
		symtab->mod_name = "kernel";
		return 1;
	} else {
		symtab->mod_name = "unknown";
		return 0;
	}
}

/*
 * kdbnearsym
 *
 *	Return the name of the symbol with the nearest address
 *	less than 'addr'.
 *
 * Parameters:
 * 	addr	Address to check for symbol near
 *      symtab  Structure to receive results
 * Outputs:
 * Returns:
 *	0	No sections contain this address, symtab zero filled
 *	1	Address mapped to module/symbol/section, data in symtab
 * Locking:
 *	None.
 * Remarks:
 */

int
kdbnearsym(unsigned long addr, kdb_symtab_t *symtab)
{
	unsigned long symsize;
	unsigned long symoffset;
	char namebuf[128];
	memset(symtab, 0, sizeof(*symtab));
	symtab->sym_name = kallsyms_lookup(addr,&symsize,&symoffset,(char **) &symtab->mod_name,namebuf);
	
	symtab->sym_start=addr-symoffset;
	symtab->sym_end=symtab->sym_start+symsize;
	if (symtab->mod_name && *symtab->mod_name == '\0')
		symtab->mod_name = "kernel";
	return 1;
}

#if defined(CONFIG_SMP)
/*
 * kdb_ipi
 *
 *	This function is called from the non-maskable interrupt
 *	handler to handle a kdb IPI instruction.
 *
 * Inputs:
 *	regs	= Exception frame pointer
 * Outputs:
 *	None.
 * Returns:
 *	0	- Did not handle NMI
 *	1	- Handled NMI
 * Locking:
 *	None.
 * Remarks:
 *	Initially one processor is invoked in the kdb() code.  That
 *	processor sends an ipi which drives this routine on the other
 *	processors.  All this does is call kdb() with reason SWITCH.
 *	This puts all processors into the kdb() routine and all the
 *	code for breakpoints etc. is in one place.
 *	One problem with the way the kdb NMI is sent, the NMI has no
 *	identification that says it came from kdb.  If the cpu's kdb state is
 *	marked as "waiting for kdb_ipi" then the NMI is treated as coming from
 *	kdb, otherwise it is assumed to be for another reason and is ignored.
 */

int
kdb_ipi(struct pt_regs *regs, void (*ack_interrupt)(void))
{
	/* Do not print before checking and clearing WAIT_IPI, IPIs are
	 * going all the time.
	 */
	if (KDB_STATE(WAIT_IPI)) {
		/*
		 * Stopping other processors via smp_kdb_stop().
		 */
		if (ack_interrupt)
			(*ack_interrupt)();	/* Acknowledge the interrupt */
		KDB_STATE_CLEAR(WAIT_IPI);
		KDB_DEBUG_STATE("kdb_ipi 1", 0);
		kdb(KDB_REASON_SWITCH, 0, regs);	/* Spin in kdb() */
		KDB_DEBUG_STATE("kdb_ipi 2", 0);
		return 1;
	}
	return 0;
}
#endif	/* CONFIG_SMP */

void
kdb_enablehwfault(void)
{
	kdba_enable_mce();
}

/*
 * kdb_get_next_ar
 *
 *	Get the next activation record from the stack.
 *
 * Inputs:
 *	arend	Last byte +1 of the activation record.  sp for the first
 *		frame, start of callee's activation record otherwise.
 *	func	Start address of function.
 *	pc	Current program counter within this function.  pc for
 *		the first frame, caller's return address otherwise.
 *	fp	Current frame pointer.  Register fp for the first
 *		frame, oldfp otherwise.  0 if not known.
 *	ss	Start of stack for the current process.
 * Outputs:
 *	ar	Activation record.
 *	symtab	kallsyms symbol table data for the calling function.
 * Returns:
 *	1 if ar is usable, 0 if not.
 * Locking:
 *	None.
 * Remarks:
 *	Activation Record format, assuming a stack that grows down
 *	(KDB_STACK_DIRECTION == -1).
 *
 *	+-----------------------------+   ^         =====================
 *	| Return address, frame 3     |   |
 *	+-----------------------------+   |
 *	| Frame Pointer, frame 3      |>--'
 *	+-----------------------------+<--.
 *	| Locals and automatics,      |   |
 *	| frame 2. (variable size)    |   |                 AR 2
 *	+-----------------------------+   |
 *	| Save registers,             |   |
 *	| frame 2. (variable size)    |   |
 *	+-----------------------------+   |
 *	| Arguments to frame 1,       |   |
 *	| (variable size)             |   |
 *	+-----------------------------+   |         =====================
 *	| Return address, frame 2     |   |
 *	+-----------------------------+   |
 *	| Frame Pointer, frame 2      |>--'
 *	+-----------------------------+<--.
 *	| Locals and automatics,      |   |
 *	| frame 1. (variable size)    |   |                 AR 1
 *	+-----------------------------+   |
 *	| Save registers,             |   |
 *	| frame 1. (variable size)    |   |
 *	+-----------------------------+   |
 *	| Arguments to frame 0,       |   |
 *	| (variable size)             |   |
 *	+-----------------------------+   |  -- (5) =====================
 *	| Return address, frame 1     |   |
 *	+-----------------------------+   |  -- (0)
 *	| Frame Pointer, frame 1      |>--'
 *	+-----------------------------+      -- (1), (2)
 *	| Locals and automatics,      |
 *	| frame 0. (variable size)    |                     AR 0
 *	+-----------------------------+      -- (3)
 *	| Save registers,             |
 *	| frame 0. (variable size)    |
 *	+-----------------------------+      -- (4) =====================
 *
 * The stack for the top frame can be in one of several states.
 *  (0) Immediately on entry to the function, stack pointer (sp) is
 *      here.
 *  (1) If the function was compiled with frame pointers and the 'push
 *      fp' instruction has been executed then the pointer to the
 *      previous frame is on the stack.  However there is no guarantee
 *      that this saved pointer is valid, the calling function might
 *      not have frame pointers.  sp is adjusted by wordsize after
 *      'push fp'.
 *  (2) If the function was compiled with frame pointers and the 'copy
 *      sp to fp' instruction has been executed then fp points here.
 *  (3) If the function startup has 'adjust sp by 0xnn bytes' and that
 *      instruction has been executed then sp has been adjusted by
 *      0xnn bytes for local and automatic variables.
 *  (4) If the function startup has one or more 'push reg' instructions
 *      and any have been executed then sp has been adjusted by
 *      wordsize bytes for each register saved.
 *
 * As the function exits it rewinds the stack, typically to (1) then (0).
 *
 * The stack entries for the lower frames is normally are in state (5).
 *  (5) Arguments for the called frame are on to the stack.
 * However lower frames can be incomplete if there is an interrupt in
 * progress.
 *
 * An activation record runs from the return address for a function
 * through to the return address for the next function or sp, whichever
 * comes first.  For each activation record we extract :-
 *
 *   start    Address of the activation record.
 *   end      Address of the last byte+1 in the activation record.
 *   ret      Return address to caller.
 *   oldfp    Frame pointer to previous frame, 0 if this function was
 *            not compiled with frame pointers.
 *   fp       Frame pointer for the current frame, 0 if this function
 *            was not compiled with frame pointers or fp has not been
 *            set yet.
 *   arg0     Address of the first argument (in the previous activation
 *            record).
 *   locals   Bytes allocated to locals and automatics.
 *   regs     Bytes allocated to saved registers.
 *   args     Bytes allocated to arguments (in the previous activation
 *            record).
 *   setup    Bytes allocated to setup data on stack (return address,
 *	      frame pointer).
 *
 * Although the kernel might be compiled with frame pointers, we still
 * have to assume the worst and validate the frame.  Some calls from
 * asm code to C code might not use frame pointers.  Third party binary
 * only modules might be compiled without frame pointers, even when the
 * rest of the kernel has frame pointers.  Some routines are always
 * compiled with frame pointers, even if the overall kernel is not.  A
 * routine compiled with frame pointers can be called from a routine
 * without frame pointers, the previous "frame pointer" is saved on
 * stack but it contains garbage.
 *
 * We check the object code to see if it saved a frame pointer and we
 * validate that pointer.  Basically frame pointers are hints.
 */

#define FORCE_ARG(ar,n)	(ar)->setup = (ar)->locals = (ar)->regs = \
			(ar)->fp = (ar)->oldfp = (ar)->ret = 0; \
			(ar)->start = (ar)->end - KDB_STACK_DIRECTION*(n)*sizeof(unsigned long);

int
kdb_get_next_ar(kdb_machreg_t arend, kdb_machreg_t func,
		kdb_machreg_t pc, kdb_machreg_t fp, kdb_machreg_t ss,
		kdb_ar_t *ar, kdb_symtab_t *symtab)
{
	if (KDB_DEBUG(AR)) {
		kdb_printf("kdb_get_next_ar: arend=0x%lx func=0x%lx pc=0x%lx fp=0x%lx\n",
			arend, func, pc, fp);
	}

	memset(ar, 0, sizeof(*ar));
	if (!kdbnearsym(pc, symtab)) {
		symtab->sym_name = "<unknown>";
		symtab->mod_name = "kernel";
		if (KDB_DEBUG(AR)) {
			kdb_printf("kdb_get_next_ar: callee not in kernel\n");
		}
		pc = 0;
	}

	if (!kdba_prologue(symtab, pc, arend, fp, ss, 0, ar)) {
		if (KDB_DEBUG(AR)) {
			kdb_printf("kdb_get_next_ar: callee prologue failed\n");
		}
		return(0);
	}
	if (KDB_DEBUG(AR)) {
		kdb_printf("kdb_get_next_ar: callee activation record\n");
		kdb_printf("  start=0x%lx end=0x%lx ret=0x%lx oldfp=0x%lx fp=0x%lx\n",
			ar->start, ar->end, ar->ret, ar->oldfp, ar->fp);
		kdb_printf("  locals=%ld regs=%ld setup=%ld\n",
			ar->locals, ar->regs, ar->setup);
	}

	if (ar->ret) {
		/* Run the caller code to get arguments to callee function */
		kdb_symtab_t	caller_symtab;
		kdb_ar_t	caller_ar;
		memset(&caller_ar, 0, sizeof(caller_ar));
		if (!kdbnearsym(ar->ret, &caller_symtab)) {
			if (KDB_DEBUG(AR)) {
				kdb_printf("kdb_get_next_ar: caller not in kernel\n");
			}
		} else if (kdba_prologue(&caller_symtab, ar->ret,
				ar->start, ar->oldfp, ss, 1, &caller_ar)) {
				/* some caller data extracted */ ;
		} else if (strcmp(symtab->sym_name, "do_exit") == 0) {
			/* non-standard caller, force one argument */
			FORCE_ARG(&caller_ar, 1);
		} else if (KDB_DEBUG(AR)) {
				kdb_printf("kdb_get_next_ar: caller prologue failed\n");
		}
		if (KDB_DEBUG(AR)) {
			kdb_printf("kdb_get_next_ar: caller activation record\n");
			kdb_printf("  start=0x%lx end=0x%lx ret=0x%lx"
				   " oldfp=0x%lx fp=0x%lx\n",
				caller_ar.start, caller_ar.end, caller_ar.ret,
				caller_ar.oldfp, caller_ar.fp);
			kdb_printf("  locals=%ld regs=%ld args=%ld setup=%ld\n",
				caller_ar.locals, caller_ar.regs,
				caller_ar.args, caller_ar.setup);
		}
		if (caller_ar.start) {
			ar->args = KDB_STACK_DIRECTION*(caller_ar.end - caller_ar.start) -
				(caller_ar.setup + caller_ar.locals + caller_ar.regs);
			if (ar->args < 0)
				ar->args = 0;
			if (ar->args) {
				ar->arg0 = ar->start -
					KDB_STACK_DIRECTION*(ar->args - 4);
				if (KDB_DEBUG(AR)) {
					kdb_printf("  callee arg0=0x%lx args=%ld\n",
						ar->arg0, ar->args);
				}
			}
		}
	}

	return(1);
}

/*
 * kdb_symbol_print
 *
 *	Standard method for printing a symbol name and offset.
 * Inputs:
 *	addr	Address to be printed.
 *	symtab	Address of symbol data, if NULL this routine does its
 *		own lookup.
 *	punc	Punctuation for string, bit field.
 * Outputs:
 *	None.
 * Returns:
 *	Always 0.
 * Locking:
 *	none.
 * Remarks:
 *	The string and its punctuation is only printed if the address
 *	is inside the kernel, except that the value is always printed
 *	when requested.
 */

void
kdb_symbol_print(kdb_machreg_t addr, const kdb_symtab_t *symtab_p, unsigned int punc)
{
	kdb_symtab_t symtab, *symtab_p2;
	if (symtab_p) {
		symtab_p2 = (kdb_symtab_t *)symtab_p;
	}
	else {
		symtab_p2 = &symtab;
		kdbnearsym(addr, symtab_p2);
	}
	if (symtab_p2->sym_name || (punc & KDB_SP_VALUE)) {
		;	/* drop through */
	}
	else {
		return;
	}
	if (punc & KDB_SP_SPACEB) {
		kdb_printf(" ");
	}
	if (punc & KDB_SP_VALUE) {
		kdb_printf(kdb_machreg_fmt0, addr);
	}
	if (!symtab_p2->sym_name) {
		return;
	}
	if (punc & KDB_SP_VALUE) {
		kdb_printf(" ");
	}
	if (punc & KDB_SP_PAREN) {
		kdb_printf("(");
	}
	if (symtab_p2->mod_name)
		if (strcmp(symtab_p2->mod_name, "kernel")) {
			kdb_printf("[%s]", symtab_p2->mod_name);
		}
	kdb_printf("%s", symtab_p2->sym_name);
	if (addr != symtab_p2->sym_start) {
		kdb_printf("+0x%lx", addr - symtab_p2->sym_start);
	}
	if (punc & KDB_SP_SYMSIZE) {
		kdb_printf("/0x%lx", symtab_p2->sym_end - symtab_p2->sym_start);
	}
	if (punc & KDB_SP_PAREN) {
		kdb_printf(")");
	}
	if (punc & KDB_SP_SPACEA) {
		kdb_printf(" ");
	}
	if (punc & KDB_SP_NEWLINE) {
		kdb_printf("\n");
	}
}

/*
 * kdb_strdup
 *
 *	kdb equivalent of strdup, for disasm code.
 * Inputs:
 *	str	The string to duplicate.
 *	type	Flags to kmalloc for the new string.
 * Outputs:
 *	None.
 * Returns:
 *	Address of the new string, NULL if storage could not be allocated.
 * Locking:
 *	none.
 * Remarks:
 *	This is not in lib/string.c because it uses kmalloc which is not
 *	available when string.o is used in boot loaders.
 */

char *kdb_strdup(const char *str, int type)
{
	int n = strlen(str)+1;
	char *s = kmalloc(n, type);
	if (!s) return NULL;
	return strcpy(s, str);
}

/*
 * kdb_getarea_size
 *
 *	Read an area of data.  The kdb equivalent of copy_from_user, with
 *	kdb messages for invalid addresses.
 * Inputs:
 *	res	Pointer to the area to receive the result.
 *	addr	Address of the area to copy.
 *	size	Size of the area.
 * Outputs:
 *	none.
 * Returns:
 *	0 for success, < 0 for error.
 * Locking:
 *	none.
 */

int kdb_getarea_size(void *res, unsigned long addr, size_t size)
{
	int ret = kdba_getarea_size(res, addr, size);
	if (ret) {
		if (!KDB_STATE(SUPPRESS)) {
			kdb_printf("kdb_getarea: Bad address 0x%lx\n", addr);
			KDB_STATE_SET(SUPPRESS);
		}
		ret = KDB_BADADDR;
	}
	else {
		KDB_STATE_CLEAR(SUPPRESS);
	}
	return(ret);
}

/*
 * kdb_putarea_size
 *
 *	Write an area of data.  The kdb equivalent of copy_to_user, with
 *	kdb messages for invalid addresses.
 * Inputs:
 *	addr	Address of the area to write to.
 *	res	Pointer to the area holding the data.
 *	size	Size of the area.
 * Outputs:
 *	none.
 * Returns:
 *	0 for success, < 0 for error.
 * Locking:
 *	none.
 */

int kdb_putarea_size(unsigned long addr, void *res, size_t size)
{
	int ret = kdba_putarea_size(addr, res, size);
	if (ret) {
		if (!KDB_STATE(SUPPRESS)) {
			kdb_printf("kdb_putarea: Bad address 0x%lx\n", addr);
			KDB_STATE_SET(SUPPRESS);
		}
		ret = KDB_BADADDR;
	}
	else {
		KDB_STATE_CLEAR(SUPPRESS);
	}
	return(ret);
}

/*
 * kdb_getword
 *
 * 	Read a binary value.  Unlike kdb_getarea, this treats data as numbers.
 * Inputs:
 *	word	Pointer to the word to receive the result.
 *	addr	Address of the area to copy.
 *	size	Size of the area.
 * Outputs:
 *	none.
 * Returns:
 *	0 for success, < 0 for error.
 * Locking:
 *	none.
 */

int kdb_getword(unsigned long *word, unsigned long addr, size_t size)
{
	int diag;
	__u8  w1;
	__u16 w2;
	__u32 w4;
	__u64 w8;
	*word = 0;	/* Default value if addr or size is invalid */
	switch (size) {
	case 1:
		if (!(diag = kdb_getarea(w1, addr)))
			*word = w1;
		break;
	case 2:
		if (!(diag = kdb_getarea(w2, addr)))
			*word = w2;
		break;
	case 4:
		if (!(diag = kdb_getarea(w4, addr)))
			*word = w4;
		break;
	case 8:
		if (size <= sizeof(*word)) {
			if (!(diag = kdb_getarea(w8, addr)))
				*word = w8;
			break;
		}
		/* drop through */
	default:
		diag = KDB_BADWIDTH;
		kdb_printf("kdb_getword: bad width %ld\n", (long) size);
	}
	return(diag);
}

/*
 * kdb_putword
 *
 * 	Write a binary value.  Unlike kdb_putarea, this treats data as numbers.
 * Inputs:
 *	addr	Address of the area to write to..
 *	word	The value to set.
 *	size	Size of the area.
 * Outputs:
 *	none.
 * Returns:
 *	0 for success, < 0 for error.
 * Locking:
 *	none.
 */

int kdb_putword(unsigned long addr, unsigned long word, size_t size)
{
	int diag;
	__u8  w1;
	__u16 w2;
	__u32 w4;
	__u64 w8;
	switch (size) {
	case 1:
		w1 = word;
		diag = kdb_putarea(addr, w1);
		break;
	case 2:
		w2 = word;
		diag = kdb_putarea(addr, w2);
		break;
	case 4:
		w4 = word;
		diag = kdb_putarea(addr, w4);
		break;
	case 8:
		if (size <= sizeof(word)) {
			w8 = word;
			diag = kdb_putarea(addr, w8);
			break;
		}
		/* drop through */
	default:
		diag = KDB_BADWIDTH;
		kdb_printf("kdb_putword: bad width %ld\n", (long) size);
	}
	return(diag);
}

/*
 * kdb_task_state_string
 *
 * 	Convert a string containing any of the letters DRSTZU to a mask for
 * 	the process state field and return the value.  If no argument is
 * 	supplied, return ~0.
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 * Outputs:
 *	none.
 * Returns:
 *	Mask for process state.
 * Locking:
 *	none.
 */

#define UNRUNNABLE	(1UL << (8*sizeof(unsigned long) - 1))	/* unrunnable is < 0 */
#define RUNNING		(1UL << (8*sizeof(unsigned long) - 2))
#define TRACED		(1UL << (8*sizeof(unsigned long) - 3))
#define PT_PTRACED 0x00000001

unsigned long
kdb_task_state_string(int argc, const char **argv, const char **envp)
{
	long res = ~0;
	if (argc >= 1) {
		const char *s = argv[1];
		res = 0;
		while (*s) {
			switch (*s) {
			case 'D': res |= TASK_UNINTERRUPTIBLE; break;
			case 'R': res |= RUNNING; break;
			case 'S': res |= TASK_INTERRUPTIBLE; break;
			case 'T': res |= TASK_STOPPED | TRACED; break;
			case 'Z': res |= TASK_ZOMBIE; break;
			case 'U': res |= UNRUNNABLE; break;
			default:
				  kdb_printf("kdb_task_state unknown flag '%c' ignored\n", *s);
				  break;
			}
			++s;
		}
	}
	return res;
}

/*
 * kdb_task_state
 *
 * 	Return true if a process has the desired state given by the mask.
 * Inputs:
 *	p	struct task for the process
 *	mask	mask from kdb_task_state_string to select processes
 * Outputs:
 *	none.
 * Returns:
 *	True if the process matches at least one criteria defined by the mask.
 * Locking:
 *	none.
 */

unsigned long
kdb_task_state(const struct task_struct *p, unsigned long mask)
{
	return ((mask & p->state) ||
		(mask & RUNNING && p->state == 0) ||
		(mask & TRACED && p->ptrace & PT_PTRACED));
}
