/*
 * Kernel Debugger Architecture Independent Support Functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/desc.h>

static kdb_machreg_t
kdba_getcr(int regnum)
{
	kdb_machreg_t contents = 0;
	switch(regnum) {
	case 0:
		__asm__ ("movl %%cr0,%0\n\t":"=r"(contents));
		break;
	case 1:
		break;
	case 2:
		__asm__ ("movl %%cr2,%0\n\t":"=r"(contents));
		break;
	case 3:
		__asm__ ("movl %%cr3,%0\n\t":"=r"(contents));
		break;
	case 4:
		__asm__ ("movl %%cr4,%0\n\t":"=r"(contents));
		break;
	default:
		break;
	}

	return contents;
}

static void
kdba_putdr(int regnum, kdb_machreg_t contents)
{
	switch(regnum) {
	case 0:
		__asm__ ("movl %0,%%db0\n\t"::"r"(contents));
		break;
	case 1:
		__asm__ ("movl %0,%%db1\n\t"::"r"(contents));
		break;
	case 2:
		__asm__ ("movl %0,%%db2\n\t"::"r"(contents));
		break;
	case 3:
		__asm__ ("movl %0,%%db3\n\t"::"r"(contents));
		break;
	case 4:
	case 5:
		break;
	case 6:
		__asm__ ("movl %0,%%db6\n\t"::"r"(contents));
		break;
	case 7:
		__asm__ ("movl %0,%%db7\n\t"::"r"(contents));
		break;
	default:
		break;
	}
}

static kdb_machreg_t
kdba_getdr(int regnum)
{
	kdb_machreg_t contents = 0;
	switch(regnum) {
	case 0:
		__asm__ ("movl %%db0,%0\n\t":"=r"(contents));
		break;
	case 1:
		__asm__ ("movl %%db1,%0\n\t":"=r"(contents));
		break;
	case 2:
		__asm__ ("movl %%db2,%0\n\t":"=r"(contents));
		break;
	case 3:
		__asm__ ("movl %%db3,%0\n\t":"=r"(contents));
		break;
	case 4:
	case 5:
		break;
	case 6:
		__asm__ ("movl %%db6,%0\n\t":"=r"(contents));
		break;
	case 7:
		__asm__ ("movl %%db7,%0\n\t":"=r"(contents));
		break;
	default:
		break;
	}

	return contents;
}

kdb_machreg_t
kdba_getdr6(void)
{
	return kdba_getdr(6);
}

kdb_machreg_t
kdba_getdr7(void)
{
	return kdba_getdr(7);
}

void
kdba_putdr6(kdb_machreg_t contents)
{
	kdba_putdr(6, contents);
}

static void
kdba_putdr7(kdb_machreg_t contents)
{
	kdba_putdr(7, contents);
}

void
kdba_installdbreg(kdb_bp_t *bp)
{
	kdb_machreg_t dr7;

	dr7 = kdba_getdr7();

	kdba_putdr(bp->bp_hard->bph_reg, bp->bp_addr);

	dr7 |= DR7_GE;
	if (cpu_has_de)
		set_in_cr4(X86_CR4_DE);

	switch (bp->bp_hard->bph_reg){
	case 0:
		DR7_RW0SET(dr7,bp->bp_hard->bph_mode);
		DR7_LEN0SET(dr7,bp->bp_hard->bph_length);
		DR7_G0SET(dr7);
		break;
	case 1:
		DR7_RW1SET(dr7,bp->bp_hard->bph_mode);
		DR7_LEN1SET(dr7,bp->bp_hard->bph_length);
		DR7_G1SET(dr7);
		break;
	case 2:
		DR7_RW2SET(dr7,bp->bp_hard->bph_mode);
		DR7_LEN2SET(dr7,bp->bp_hard->bph_length);
		DR7_G2SET(dr7);
		break;
	case 3:
		DR7_RW3SET(dr7,bp->bp_hard->bph_mode);
		DR7_LEN3SET(dr7,bp->bp_hard->bph_length);
		DR7_G3SET(dr7);
		break;
	default:
		kdb_printf("kdb: Bad debug register!! %ld\n",
			   bp->bp_hard->bph_reg);
		break;
	}

	kdba_putdr7(dr7);
	return;
}

void
kdba_removedbreg(kdb_bp_t *bp)
{
	int regnum;
	kdb_machreg_t dr7;

	if (!bp->bp_hard)
		return;

	regnum = bp->bp_hard->bph_reg;

	dr7 = kdba_getdr7();

	kdba_putdr(regnum, 0);

	switch (regnum) {
	case 0:
		DR7_G0CLR(dr7);
		DR7_L0CLR(dr7);
		break;
	case 1:
		DR7_G1CLR(dr7);
		DR7_L1CLR(dr7);
		break;
	case 2:
		DR7_G2CLR(dr7);
		DR7_L2CLR(dr7);
		break;
	case 3:
		DR7_G3CLR(dr7);
		DR7_L3CLR(dr7);
		break;
	default:
		kdb_printf("kdb: Bad debug register!! %d\n", regnum);
		break;
	}

	kdba_putdr7(dr7);
}


/*
 * kdba_getregcontents
 *
 *	Return the contents of the register specified by the
 *	input string argument.   Return an error if the string
 *	does not match a machine register.
 *
 *	The following pseudo register names are supported:
 *	   &regs	 - Prints address of exception frame
 *	   kesp		 - Prints kernel stack pointer at time of fault
 *	   cesp		 - Prints current kernel stack pointer, inside kdb
 *	   ceflags	 - Prints current flags, inside kdb
 *	   %<regname>	 - Uses the value of the registers at the
 *			   last time the user process entered kernel
 *			   mode, instead of the registers at the time
 *			   kdb was entered.
 *
 * Parameters:
 *	regname		Pointer to string naming register
 *	regs		Pointer to structure containing registers.
 * Outputs:
 *	*contents	Pointer to unsigned long to recieve register contents
 * Returns:
 *	0		Success
 *	KDB_BADREG	Invalid register name
 * Locking:
 * 	None.
 * Remarks:
 * 	If kdb was entered via an interrupt from the kernel itself then
 *	ss and esp are *not* on the stack.
 */

static struct kdbregs {
	char   *reg_name;
	size_t	reg_offset;
} kdbreglist[] = {
	{ "eax",	offsetof(struct pt_regs, eax) },
	{ "ebx",	offsetof(struct pt_regs, ebx) },
	{ "ecx",	offsetof(struct pt_regs, ecx) },
	{ "edx",	offsetof(struct pt_regs, edx) },

	{ "esi",	offsetof(struct pt_regs, esi) },
	{ "edi",	offsetof(struct pt_regs, edi) },
	{ "esp",	offsetof(struct pt_regs, esp) },
	{ "eip",	offsetof(struct pt_regs, eip) },

	{ "ebp",	offsetof(struct pt_regs, ebp) },
	{ "xss", 	offsetof(struct pt_regs, xss) },
	{ "xcs",	offsetof(struct pt_regs, xcs) },
	{ "eflags", 	offsetof(struct pt_regs, eflags) },

	{ "xds", 	offsetof(struct pt_regs, xds) },
	{ "xes", 	offsetof(struct pt_regs, xes) },
	{ "origeax",	offsetof(struct pt_regs, orig_eax) },

};

static const int nkdbreglist = sizeof(kdbreglist) / sizeof(struct kdbregs);

static struct kdbregs dbreglist[] = {
	{ "dr0", 	0 },
	{ "dr1", 	1 },
	{ "dr2",	2 },
	{ "dr3", 	3 },
	{ "dr6", 	6 },
	{ "dr7",	7 },
};

static const int ndbreglist = sizeof(dbreglist) / sizeof(struct kdbregs);

int
kdba_getregcontents(const char *regname,
		    struct pt_regs *regs,
		    kdb_machreg_t *contents)
{
	int i;

	if (strcmp(regname, "cesp") == 0) {
		asm volatile("movl %%esp,%0":"=m" (*contents));
		return 0;
	}

	if (strcmp(regname, "ceflags") == 0) {
		unsigned long flags;
		local_save_flags(flags);
		*contents = flags;
		return 0;
	}

	if (regname[0] == '%') {
		/* User registers:  %%e[a-c]x, etc */
		regname++;
		regs = (struct pt_regs *)
			(kdb_current_task->thread.esp0 - sizeof(struct pt_regs));
	}

	for (i=0; i<ndbreglist; i++) {
		if (strnicmp(dbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < ndbreglist)
	 && (strlen(dbreglist[i].reg_name) == strlen(regname))) {
		*contents = kdba_getdr(dbreglist[i].reg_offset);
		return 0;
	}

	if (!regs) {
		kdb_printf("%s: pt_regs not available, use bt* or pid to select a different task\n", __FUNCTION__);
		return KDB_BADREG;
	}

	if (strcmp(regname, "&regs") == 0) {
		*contents = (unsigned long)regs;
		return 0;
	}

	if (strcmp(regname, "kesp") == 0) {
		*contents = (unsigned long)regs + sizeof(struct pt_regs);
		if ((regs->xcs & 0xffff) == __KERNEL_CS) {
			/* esp and ss are not on stack */
			*contents -= 2*4;
		}
		return 0;
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < nkdbreglist)
	 && (strlen(kdbreglist[i].reg_name) == strlen(regname))) {
		if ((regs->xcs & 0xffff) == __KERNEL_CS) {
			/* No cpl switch, esp and ss are not on stack */
			if (strcmp(kdbreglist[i].reg_name, "esp") == 0) {
				*contents = (kdb_machreg_t)regs +
					sizeof(struct pt_regs) - 2*4;
				return(0);
			}
			if (strcmp(kdbreglist[i].reg_name, "xss") == 0) {
				asm volatile(
					"pushl %%ss\n"
					"popl %0\n"
					:"=m" (*contents));
				return(0);
			}
		}
		*contents = *(unsigned long *)((unsigned long)regs +
				kdbreglist[i].reg_offset);
		return(0);
	}

	return KDB_BADREG;
}

/*
 * kdba_setregcontents
 *
 *	Set the contents of the register specified by the
 *	input string argument.   Return an error if the string
 *	does not match a machine register.
 *
 *	Supports modification of user-mode registers via
 *	%<register-name>
 *
 * Parameters:
 *	regname		Pointer to string naming register
 *	regs		Pointer to structure containing registers.
 *	contents	Unsigned long containing new register contents
 * Outputs:
 * Returns:
 *	0		Success
 *	KDB_BADREG	Invalid register name
 * Locking:
 * 	None.
 * Remarks:
 */

int
kdba_setregcontents(const char *regname,
		  struct pt_regs *regs,
		  unsigned long contents)
{
	int i;

	if (regname[0] == '%') {
		regname++;
		regs = (struct pt_regs *)
			(kdb_current_task->thread.esp0 - sizeof(struct pt_regs));
	}

	for (i=0; i<ndbreglist; i++) {
		if (strnicmp(dbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < ndbreglist)
	 && (strlen(dbreglist[i].reg_name) == strlen(regname))) {
		kdba_putdr(dbreglist[i].reg_offset, contents);
		return 0;
	}

	if (!regs) {
		kdb_printf("%s: pt_regs not available, use bt* or pid to select a different task\n", __FUNCTION__);
		return KDB_BADREG;
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < nkdbreglist)
	 && (strlen(kdbreglist[i].reg_name) == strlen(regname))) {
		*(unsigned long *)((unsigned long)regs
				   + kdbreglist[i].reg_offset) = contents;
		return 0;
	}

	return KDB_BADREG;
}

/*
 * kdba_dumpregs
 *
 *	Dump the specified register set to the display.
 *
 * Parameters:
 *	regs		Pointer to structure containing registers.
 *	type		Character string identifying register set to dump
 *	extra		string further identifying register (optional)
 * Outputs:
 * Returns:
 *	0		Success
 * Locking:
 * 	None.
 * Remarks:
 *	This function will dump the general register set if the type
 *	argument is NULL (struct pt_regs).   The alternate register
 *	set types supported by this function:
 *
 *	d		Debug registers
 *	c		Control registers
 *	u		User registers at most recent entry to kernel
 *			for the process currently selected with "pid" command.
 * Following not yet implemented:
 *	r		Memory Type Range Registers (extra defines register)
 *
 * MSR on i386/x86_64 are handled by rdmsr/wrmsr commands.
 */

int
kdba_dumpregs(struct pt_regs *regs,
	    const char *type,
	    const char *extra)
{
	int i;
	int count = 0;

	if (type
	 && (type[0] == 'u')) {
		type = NULL;
		regs = (struct pt_regs *)
			(kdb_current_task->thread.esp0 - sizeof(struct pt_regs));
	}

	if (type == NULL) {
		struct kdbregs *rlp;
		kdb_machreg_t contents;

		if (!regs) {
			kdb_printf("%s: pt_regs not available, use bt* or pid to select a different task\n", __FUNCTION__);
			return KDB_BADREG;
		}

		for (i=0, rlp=kdbreglist; i<nkdbreglist; i++,rlp++) {
			kdb_printf("%s = ", rlp->reg_name);
			kdba_getregcontents(rlp->reg_name, regs, &contents);
			kdb_printf("0x%08lx ", contents);
			if ((++count % 4) == 0)
				kdb_printf("\n");
		}

		kdb_printf("&regs = 0x%p\n", regs);

		return 0;
	}

	switch (type[0]) {
	case 'd':
	{
		unsigned long dr[8];

		for(i=0; i<8; i++) {
			if ((i == 4) || (i == 5)) continue;
			dr[i] = kdba_getdr(i);
		}
		kdb_printf("dr0 = 0x%08lx  dr1 = 0x%08lx  dr2 = 0x%08lx  dr3 = 0x%08lx\n",
			   dr[0], dr[1], dr[2], dr[3]);
		kdb_printf("dr6 = 0x%08lx  dr7 = 0x%08lx\n",
			   dr[6], dr[7]);
		return 0;
	}
	case 'c':
	{
		unsigned long cr[5];

		for (i=0; i<5; i++) {
			cr[i] = kdba_getcr(i);
		}
		kdb_printf("cr0 = 0x%08lx  cr1 = 0x%08lx  cr2 = 0x%08lx  cr3 = 0x%08lx\ncr4 = 0x%08lx\n",
			   cr[0], cr[1], cr[2], cr[3], cr[4]);
		return 0;
	}
	case 'r':
		break;
	default:
		return KDB_BADREG;
	}

	/* NOTREACHED */
	return 0;
}
EXPORT_SYMBOL(kdba_dumpregs);

kdb_machreg_t
kdba_getpc(struct pt_regs *regs)
{
	return regs ? regs->eip : 0;
}

int
kdba_setpc(struct pt_regs *regs, kdb_machreg_t newpc)
{
	if (KDB_NULL_REGS(regs))
		return KDB_BADREG;
	regs->eip = newpc;
	KDB_STATE_SET(IP_ADJUSTED);
	return 0;
}

/*
 * kdba_main_loop
 *
 *	Do any architecture specific set up before entering the main kdb loop.
 *	The primary function of this routine is to make all processes look the
 *	same to kdb, kdb must be able to list a process without worrying if the
 *	process is running or blocked, so make all process look as though they
 *	are blocked.
 *
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	error2		kdb's current reason code.  Initially error but can change
 *			acording to kdb state.
 *	db_result	Result from break or debug point.
 *	regs		The exception frame at time of fault/breakpoint.  If reason
 *			is SILENT or CPU_UP then regs is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Outputs:
 *	Sets eip and esp in current->thread.
 * Locking:
 *	None.
 * Remarks:
 *	none.
 */

int
kdba_main_loop(kdb_reason_t reason, kdb_reason_t reason2, int error,
	       kdb_dbtrap_t db_result, struct pt_regs *regs)
{
	int ret;
	ret = kdb_save_running(regs, reason, reason2, error, db_result);
	kdb_unsave_running(regs);
	return ret;
}

void
kdba_disableint(kdb_intstate_t *state)
{
	unsigned long *fp = (unsigned long *)state;
	unsigned long flags;

	local_irq_save(flags);

	*fp = flags;
}

void
kdba_restoreint(kdb_intstate_t *state)
{
	unsigned long flags = *(int *)state;
	local_irq_restore(flags);
}

void
kdba_setsinglestep(struct pt_regs *regs)
{
	if (KDB_NULL_REGS(regs))
		return;
	if (regs->eflags & EF_IE)
		KDB_STATE_SET(A_IF);
	else
		KDB_STATE_CLEAR(A_IF);
	regs->eflags = (regs->eflags | EF_TF) & ~EF_IE;
}

void
kdba_clearsinglestep(struct pt_regs *regs)
{
	if (KDB_NULL_REGS(regs))
		return;
	if (KDB_STATE(A_IF))
		regs->eflags |= EF_IE;
	else
		regs->eflags &= ~EF_IE;
}

int asmlinkage
kdba_setjmp(kdb_jmp_buf *jb)
{
#if defined(CONFIG_FRAME_POINTER)
	__asm__ ("movl 8(%esp), %eax\n\t"
		 "movl %ebx, 0(%eax)\n\t"
		 "movl %esi, 4(%eax)\n\t"
		 "movl %edi, 8(%eax)\n\t"
		 "movl (%esp), %ecx\n\t"
		 "movl %ecx, 12(%eax)\n\t"
		 "leal 8(%esp), %ecx\n\t"
		 "movl %ecx, 16(%eax)\n\t"
		 "movl 4(%esp), %ecx\n\t"
		 "movl %ecx, 20(%eax)\n\t");
#else	 /* CONFIG_FRAME_POINTER */
	__asm__ ("movl 4(%esp), %eax\n\t"
		 "movl %ebx, 0(%eax)\n\t"
		 "movl %esi, 4(%eax)\n\t"
		 "movl %edi, 8(%eax)\n\t"
		 "movl %ebp, 12(%eax)\n\t"
		 "leal 4(%esp), %ecx\n\t"
		 "movl %ecx, 16(%eax)\n\t"
		 "movl 0(%esp), %ecx\n\t"
		 "movl %ecx, 20(%eax)\n\t");
#endif   /* CONFIG_FRAME_POINTER */
	return 0;
}

void asmlinkage
kdba_longjmp(kdb_jmp_buf *jb, int reason)
{
#if defined(CONFIG_FRAME_POINTER)
	__asm__("movl 8(%esp), %ecx\n\t"
		"movl 12(%esp), %eax\n\t"
		"movl 20(%ecx), %edx\n\t"
		"movl 0(%ecx), %ebx\n\t"
		"movl 4(%ecx), %esi\n\t"
		"movl 8(%ecx), %edi\n\t"
		"movl 12(%ecx), %ebp\n\t"
		"movl 16(%ecx), %esp\n\t"
		"jmp *%edx\n");
#else    /* CONFIG_FRAME_POINTER */
	__asm__("movl 4(%esp), %ecx\n\t"
		"movl 8(%esp), %eax\n\t"
		"movl 20(%ecx), %edx\n\t"
		"movl 0(%ecx), %ebx\n\t"
		"movl 4(%ecx), %esi\n\t"
		"movl 8(%ecx), %edi\n\t"
		"movl 12(%ecx), %ebp\n\t"
		"movl 16(%ecx), %esp\n\t"
		"jmp *%edx\n");
#endif	 /* CONFIG_FRAME_POINTER */
}

/*
 * kdba_pt_regs
 *
 *	Format a struct pt_regs
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	If no address is supplied, it uses the last irq pt_regs.
 */

static int
kdba_pt_regs(int argc, const char **argv)
{
	int diag;
	kdb_machreg_t addr;
	long offset = 0;
	int nextarg;
	struct pt_regs *p;
	static const char *fmt = "  %-11.11s 0x%lx\n";

	if (argc == 0) {
		addr = (kdb_machreg_t) get_irq_regs();
	} else if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
		if (diag)
			return diag;
	} else {
		return KDB_ARGCOUNT;
	}

	p = (struct pt_regs *) addr;
	kdb_printf("struct pt_regs 0x%p-0x%p\n", p, (unsigned char *)p + sizeof(*p) - 1);
	kdb_print_nameval("ebx", p->ebx);
	kdb_print_nameval("ecx", p->ecx);
	kdb_print_nameval("edx", p->edx);
	kdb_print_nameval("esi", p->esi);
	kdb_print_nameval("edi", p->edi);
	kdb_print_nameval("ebp", p->ebp);
	kdb_print_nameval("eax", p->eax);
	kdb_printf(fmt, "xds", p->xds);
	kdb_printf(fmt, "xes", p->xes);
	kdb_print_nameval("orig_eax", p->orig_eax);
	kdb_print_nameval("eip", p->eip);
	kdb_printf(fmt, "xcs", p->xcs);
	kdb_printf(fmt, "eflags", p->eflags);
	kdb_printf(fmt, "esp", p->esp);
	kdb_printf(fmt, "xss", p->xss);
	return 0;
}

/*
 * kdba_stackdepth
 *
 *	Print processes that are using more than a specific percentage of their
 *	stack.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	If no percentage is supplied, it uses 60.
 */

static void
kdba_stackdepth1(struct task_struct *p, unsigned long esp)
{
	struct thread_info *tinfo;
	int used;
	const char *type;
	kdb_ps1(p);
	do {
		tinfo = (struct thread_info *)(esp & -THREAD_SIZE);
		used = sizeof(*tinfo) + THREAD_SIZE - (esp & (THREAD_SIZE-1));
		type = NULL;
		if (kdb_task_has_cpu(p)) {
			struct kdb_activation_record ar;
			memset(&ar, 0, sizeof(ar));
			kdba_get_stack_info_alternate(esp, -1, &ar);
			type = ar.stack.id;
		}
		if (!type)
			type = "process";
		kdb_printf("  %s stack %p esp %lx used %d\n", type, tinfo, esp, used);
		esp = tinfo->previous_esp;
	} while (esp);
}

static int
kdba_stackdepth(int argc, const char **argv)
{
	int diag, cpu, threshold, used, over;
	unsigned long percentage;
	unsigned long esp;
	long offset = 0;
	int nextarg;
	struct task_struct *p, *g;
	struct kdb_running_process *krp;
	struct thread_info *tinfo;

	if (argc == 0) {
		percentage = 60;
	} else if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &percentage, &offset, NULL);
		if (diag)
			return diag;
	} else {
		return KDB_ARGCOUNT;
	}
	percentage = max_t(int, percentage, 1);
	percentage = min_t(int, percentage, 100);
	threshold = ((2 * THREAD_SIZE * percentage) / 100 + 1) >> 1;
	kdb_printf("stackdepth: processes using more than %ld%% (%d bytes) of stack\n",
		percentage, threshold);

	/* Run the active tasks first, they can have multiple stacks */
	for (cpu = 0, krp = kdb_running_process; cpu < NR_CPUS; ++cpu, ++krp) {
		if (!cpu_online(cpu))
			continue;
		p = krp->p;
		esp = krp->arch.esp;
		over = 0;
		do {
			tinfo = (struct thread_info *)(esp & -THREAD_SIZE);
			used = sizeof(*tinfo) + THREAD_SIZE - (esp & (THREAD_SIZE-1));
			if (used >= threshold)
				over = 1;
			esp = tinfo->previous_esp;
		} while (esp);
		if (over)
			kdba_stackdepth1(p, krp->arch.esp);
	}
	/* Now the tasks that are not on cpus */
	kdb_do_each_thread(g, p) {
		if (kdb_task_has_cpu(p))
			continue;
		esp = p->thread.esp;
		used = sizeof(*tinfo) + THREAD_SIZE - (esp & (THREAD_SIZE-1));
		over = used >= threshold;
		if (over)
			kdba_stackdepth1(p, esp);
	} kdb_while_each_thread(g, p);

	return 0;
}

asmlinkage int kdb_call(void);

/* Executed once on each cpu at startup. */
void
kdba_cpu_up(void)
{
}

static int __init
kdba_arch_init(void)
{
#ifdef	CONFIG_SMP
	set_intr_gate(KDB_VECTOR, kdb_interrupt);
#endif
	set_intr_gate(KDBENTER_VECTOR, kdb_call);
	return 0;
}

arch_initcall(kdba_arch_init);

/*
 * kdba_init
 *
 * 	Architecture specific initialization.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	None.
 */

void __init
kdba_init(void)
{
	kdba_arch_init();	/* Need to register KDBENTER_VECTOR early */
	kdb_register("pt_regs", kdba_pt_regs, "address", "Format struct pt_regs", 0);
	kdb_register("stackdepth", kdba_stackdepth, "[percentage]", "Print processes using >= stack percentage", 0);

	return;
}

/*
 * kdba_adjust_ip
 *
 * 	Architecture specific adjustment of instruction pointer before leaving
 *	kdb.
 *
 * Parameters:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	regs		The exception frame at time of fault/breakpoint.  If reason
 *			is SILENT or CPU_UP then regs is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	noop on ix86.
 */

void
kdba_adjust_ip(kdb_reason_t reason, int error, struct pt_regs *regs)
{
	return;
}

void
kdba_set_current_task(const struct task_struct *p)
{
	kdb_current_task = p;
	if (kdb_task_has_cpu(p)) {
		struct kdb_running_process *krp = kdb_running_process + kdb_process_cpu(p);
		kdb_current_regs = krp->regs;
		return;
	}
	kdb_current_regs = NULL;
}

/*
 * asm-i386 uaccess.h supplies __copy_to_user which relies on MMU to
 * trap invalid addresses in the _xxx fields.  Verify the other address
 * of the pair is valid by accessing the first and last byte ourselves,
 * then any access violations should only be caused by the _xxx
 * addresses,
 */

int
kdba_putarea_size(unsigned long to_xxx, void *from, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int r;
	char c;
	c = *((volatile char *)from);
	c = *((volatile char *)from + size - 1);

	if (to_xxx < PAGE_OFFSET) {
		return kdb_putuserarea_size(to_xxx, from, size);
	}

	set_fs(KERNEL_DS);
	r = __copy_to_user_inatomic((void __user *)to_xxx, from, size);
	set_fs(oldfs);
	return r;
}

int
kdba_getarea_size(void *to, unsigned long from_xxx, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int r;
	*((volatile char *)to) = '\0';
	*((volatile char *)to + size - 1) = '\0';

	if (from_xxx < PAGE_OFFSET) {
		return kdb_getuserarea_size(to, from_xxx, size);
	}

	set_fs(KERNEL_DS);
	switch (size) {
	case 1:
		r = __copy_to_user_inatomic((void __user *)to, (void *)from_xxx, 1);
		break;
	case 2:
		r = __copy_to_user_inatomic((void __user *)to, (void *)from_xxx, 2);
		break;
	case 4:
		r = __copy_to_user_inatomic((void __user *)to, (void *)from_xxx, 4);
		break;
	case 8:
		r = __copy_to_user_inatomic((void __user *)to, (void *)from_xxx, 8);
		break;
	default:
		r = __copy_to_user_inatomic((void __user *)to, (void *)from_xxx, size);
		break;
	}
	set_fs(oldfs);
	return r;
}

int
kdba_verify_rw(unsigned long addr, size_t size)
{
	unsigned char data[size];
	return(kdba_getarea_size(data, addr, size) || kdba_putarea_size(addr, data, size));
}

#ifdef	CONFIG_SMP

#include <mach_ipi.h>

/* When first entering KDB, try a normal IPI.  That reduces backtrace problems
 * on the other cpus.
 */
void
smp_kdb_stop(void)
{
	if (!KDB_FLAG(NOIPI))
		send_IPI_allbutself(KDB_VECTOR);
}

/* The normal KDB IPI handler */
void
smp_kdb_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	ack_APIC_irq();
	irq_enter();
	kdb_ipi(regs, NULL);
	irq_exit();
	set_irq_regs(old_regs);
}

/* Invoked once from kdb_wait_for_cpus when waiting for cpus.  For those cpus
 * that have not responded to the normal KDB interrupt yet, hit them with an
 * NMI event.
 */
void
kdba_wait_for_cpus(void)
{
	int c;
	if (KDB_FLAG(CATASTROPHIC))
		return;
	kdb_printf("  Sending NMI to cpus that have not responded yet\n");
	for_each_online_cpu(c)
		if (kdb_running_process[c].seqno < kdb_seqno - 1)
			send_IPI_mask(cpumask_of_cpu(c), NMI_VECTOR);
}

#endif	/* CONFIG_SMP */
