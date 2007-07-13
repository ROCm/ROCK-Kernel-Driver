/*
 * Kernel Debugger Architecture Independent Support Functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/mach_apic.h>
#include <asm/hw_irq.h>
#include <asm/desc.h>

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
	kdb_machreg_t	dr7;

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
	int 		regnum;
	kdb_machreg_t	dr7;

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

kdb_machreg_t
kdba_getdr(int regnum)
{
	kdb_machreg_t contents = 0;
	switch(regnum) {
	case 0:
		__asm__ ("movq %%db0,%0\n\t":"=r"(contents));
		break;
	case 1:
		__asm__ ("movq %%db1,%0\n\t":"=r"(contents));
		break;
	case 2:
		__asm__ ("movq %%db2,%0\n\t":"=r"(contents));
		break;
	case 3:
		__asm__ ("movq %%db3,%0\n\t":"=r"(contents));
		break;
	case 4:
	case 5:
		break;
	case 6:
		__asm__ ("movq %%db6,%0\n\t":"=r"(contents));
		break;
	case 7:
		__asm__ ("movq %%db7,%0\n\t":"=r"(contents));
		break;
	default:
		break;
	}

	return contents;
}


kdb_machreg_t
kdb_getcr(int regnum)
{
	kdb_machreg_t contents = 0;
	switch(regnum) {
	case 0:
		__asm__ ("movq %%cr0,%0\n\t":"=r"(contents));
		break;
	case 1:
		break;
	case 2:
		__asm__ ("movq %%cr2,%0\n\t":"=r"(contents));
		break;
	case 3:
		__asm__ ("movq %%cr3,%0\n\t":"=r"(contents));
		break;
	case 4:
		__asm__ ("movq %%cr4,%0\n\t":"=r"(contents));
		break;
	default:
		break;
	}

	return contents;
}

void
kdba_putdr(int regnum, kdb_machreg_t contents)
{
	switch(regnum) {
	case 0:
		__asm__ ("movq %0,%%db0\n\t"::"r"(contents));
		break;
	case 1:
		__asm__ ("movq %0,%%db1\n\t"::"r"(contents));
		break;
	case 2:
		__asm__ ("movq %0,%%db2\n\t"::"r"(contents));
		break;
	case 3:
		__asm__ ("movq %0,%%db3\n\t"::"r"(contents));
		break;
	case 4:
	case 5:
		break;
	case 6:
		__asm__ ("movq %0,%%db6\n\t"::"r"(contents));
		break;
	case 7:
		__asm__ ("movq %0,%%db7\n\t"::"r"(contents));
		break;
	default:
		break;
	}
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
 *	   krsp		 - Prints kernel stack pointer at time of fault
 *	   crsp		 - Prints current kernel stack pointer, inside kdb
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
 *	ss and rsp are *not* on the stack.
 */

static struct kdbregs {
	char   *reg_name;
	size_t	reg_offset;
} kdbreglist[] = {
	{ "r15",	offsetof(struct pt_regs, r15) },
	{ "r14",	offsetof(struct pt_regs, r14) },
	{ "r13",	offsetof(struct pt_regs, r13) },
	{ "r12",	offsetof(struct pt_regs, r12) },
	{ "rbp",	offsetof(struct pt_regs, rbp) },
	{ "rbx",	offsetof(struct pt_regs, rbx) },
	{ "r11",	offsetof(struct pt_regs, r11) },
	{ "r10",	offsetof(struct pt_regs, r10) },
	{ "r9",		offsetof(struct pt_regs, r9) },
	{ "r8",		offsetof(struct pt_regs, r8) },
	{ "rax",	offsetof(struct pt_regs, rax) },
	{ "rcx",	offsetof(struct pt_regs, rcx) },
	{ "rdx",	offsetof(struct pt_regs, rdx) },
	{ "rsi",	offsetof(struct pt_regs, rsi) },
	{ "rdi",	offsetof(struct pt_regs, rdi) },
	{ "orig_rax",	offsetof(struct pt_regs, orig_rax) },
	{ "rip",	offsetof(struct pt_regs, rip) },
	{ "cs",		offsetof(struct pt_regs, cs) },
	{ "eflags", 	offsetof(struct pt_regs, eflags) },
	{ "rsp",	offsetof(struct pt_regs, rsp) },
	{ "ss",		offsetof(struct pt_regs, ss) },
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

	if (strcmp(regname, "&regs") == 0) {
		*contents = (unsigned long)regs;
		return 0;
	}

	if (strcmp(regname, "krsp") == 0) {
		*contents = (unsigned long)regs + sizeof(struct pt_regs);
		if ((regs->cs & 0xffff) == __KERNEL_CS) {
			/* rsp and ss are not on stack */
			*contents -= 2*4;
		}
		return 0;
	}

	if (strcmp(regname, "crsp") == 0) {
		asm volatile("movq %%rsp,%0":"=m" (*contents));
		return 0;
	}

	if (strcmp(regname, "ceflags") == 0) {
		unsigned long flags;
		local_save_flags(flags);
		*contents = flags;
		return 0;
	}

	if (regname[0] == '%') {
		/* User registers:  %%r[a-c]x, etc */
		regname++;
		regs = (struct pt_regs *)
			(current->thread.rsp0 - sizeof(struct pt_regs));
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0)
			break;
	}

	if ((i < nkdbreglist)
	 && (strlen(kdbreglist[i].reg_name) == strlen(regname))) {
		if ((regs->cs & 0xffff) == __KERNEL_CS) {
			/* No cpl switch, rsp is not on stack */
			if (strcmp(kdbreglist[i].reg_name, "rsp") == 0) {
				*contents = (kdb_machreg_t)regs +
					sizeof(struct pt_regs) - 2*8;
				return(0);
			}
#if 0	/* FIXME */
			if (strcmp(kdbreglist[i].reg_name, "ss") == 0) {
				kdb_machreg_t r;

				r = (kdb_machreg_t)regs +
					sizeof(struct pt_regs) - 2*8;
				*contents = (kdb_machreg_t)SS(r);	/* XXX */
				return(0);
			}
#endif
		}
		*contents = *(unsigned long *)((unsigned long)regs +
				kdbreglist[i].reg_offset);
		return(0);
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
			(current->thread.rsp0 - sizeof(struct pt_regs));
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
			(current->thread.rsp0 - sizeof(struct pt_regs));
	}

	if (type == NULL) {
		struct kdbregs *rlp;
		kdb_machreg_t contents;

		for (i=0, rlp=kdbreglist; i<nkdbreglist; i++,rlp++) {
			kdb_printf("%8s = ", rlp->reg_name);
			kdba_getregcontents(rlp->reg_name, regs, &contents);
			kdb_printf("0x%016lx ", contents);
			if ((++count % 2) == 0)
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
			cr[i] = kdb_getcr(i);
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
	return regs->rip;
}

int
kdba_setpc(struct pt_regs *regs, kdb_machreg_t newpc)
{
	if (KDB_NULL_REGS(regs))
		return KDB_BADREG;
	regs->rip = newpc;
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
 *	ef		The exception frame at time of fault/breakpoint.  If reason
 *			is SILENT or CPU_UP then regs is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Outputs:
 *	Sets rip and rsp in current->thread.
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

	if (regs)
		kdba_getregcontents("rsp", regs, &(current->thread.rsp));
	kdb_save_running(regs);
	ret = kdb_main_loop(reason, reason2, error, db_result, regs);
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
	unsigned long flags = *(unsigned long *)state;
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
#ifdef	CONFIG_FRAME_POINTER
	__asm__ __volatile__
		("movq %%rbx, (0*8)(%%rdi);"
		"movq %%rcx, (1*8)(%%rdi);"
		"movq %%r12, (2*8)(%%rdi);"
		"movq %%r13, (3*8)(%%rdi);"
		"movq %%r14, (4*8)(%%rdi);"
		"movq %%r15, (5*8)(%%rdi);"
		"leaq 16(%%rsp), %%rdx;"
		"movq %%rdx, (6*8)(%%rdi);"
		"movq %%rax, (7*8)(%%rdi)"
		:
		: "a" (__builtin_return_address(0)),
		  "c" (__builtin_frame_address(1))
		);
#else	 /* !CONFIG_FRAME_POINTER */
	__asm__ __volatile__
		("movq %%rbx, (0*8)(%%rdi);"
		"movq %%rbp, (1*8)(%%rdi);"
		"movq %%r12, (2*8)(%%rdi);"
		"movq %%r13, (3*8)(%%rdi);"
		"movq %%r14, (4*8)(%%rdi);"
		"movq %%r15, (5*8)(%%rdi);"
		"leaq 8(%%rsp), %%rdx;"
		"movq %%rdx, (6*8)(%%rdi);"
		"movq %%rax, (7*8)(%%rdi)"
		:
		: "a" (__builtin_return_address(0))
		);
#endif   /* CONFIG_FRAME_POINTER */
	return 0;
}

void asmlinkage
kdba_longjmp(kdb_jmp_buf *jb, int reason)
{
	__asm__("movq (0*8)(%rdi),%rbx;"
		"movq (1*8)(%rdi),%rbp;"
		"movq (2*8)(%rdi),%r12;"
		"movq (3*8)(%rdi),%r13;"
		"movq (4*8)(%rdi),%r14;"
		"movq (5*8)(%rdi),%r15;"
		"movq (7*8)(%rdi),%rdx;"
		"movq (6*8)(%rdi),%rsp;"
		"mov %rsi, %rax;"
		"jmpq *%rdx");
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
 *	If no address is supplied, it uses the current irq pt_regs.
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
	static int first_time = 1;

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
	if (first_time) {
		first_time = 0;
		kdb_printf("\n+++ Warning: x86_64 pt_regs are not always "
			   "completely defined, r15-rbx may be invalid\n\n");
	}
	kdb_printf("struct pt_regs 0x%p-0x%p\n", p, (unsigned char *)p + sizeof(*p) - 1);
	kdb_print_nameval("r15", p->r15);
	kdb_print_nameval("r14", p->r14);
	kdb_print_nameval("r13", p->r13);
	kdb_print_nameval("r12", p->r12);
	kdb_print_nameval("rbp", p->rbp);
	kdb_print_nameval("rbx", p->rbx);
	kdb_print_nameval("r11", p->r11);
	kdb_print_nameval("r10", p->r10);
	kdb_print_nameval("r9", p->r9);
	kdb_print_nameval("r8", p->r8);
	kdb_print_nameval("rax", p->rax);
	kdb_print_nameval("rcx", p->rcx);
	kdb_print_nameval("rdx", p->rdx);
	kdb_print_nameval("rsi", p->rsi);
	kdb_print_nameval("rdi", p->rdi);
	kdb_print_nameval("orig_rax", p->orig_rax);
	kdb_print_nameval("rip", p->rip);
	kdb_printf(fmt, "cs", p->cs);
	kdb_printf(fmt, "eflags", p->eflags);
	kdb_printf(fmt, "rsp", p->rsp);
	kdb_printf(fmt, "ss", p->ss);
	return 0;
}

/*
 * kdba_cpu_pda
 *
 *	Format a struct cpu_pda
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
 *	If no cpu is supplied, it prints the current cpu.  If the cpu is '*'
 *	then it prints all cpus.
 */

static int
kdba_cpu_pda(int argc, const char **argv)
{
	int diag, nextarg, all_cpus = 0;
	long offset = 0;
	unsigned long cpu;
	struct x8664_pda *c;
	static const char *fmtl = "  %-17.17s 0x%lx\n";
	static const char *fmtd = "  %-17.17s %d\n";
	static const char *fmtp = "  %-17.17s 0x%p\n";

	if (argc == 0) {
		cpu = smp_processor_id();
	} else if (argc == 1) {
		if (strcmp(argv[1], "*") == 0) {
			all_cpus = 1;
			cpu = 0;
		} else {
			nextarg = 1;
			diag = kdbgetaddrarg(argc, argv, &nextarg, &cpu, &offset, NULL);
			if (diag)
				return diag;
		}
	} else {
		return KDB_ARGCOUNT;
	}

	for (; cpu < NR_CPUS; ++cpu) {
		if (cpu_online(cpu)) {
			c = cpu_pda(cpu);
			kdb_printf("struct cpu_pda 0x%p-0x%p\n", c, (unsigned char *)c + sizeof(*c) - 1);
			kdb_printf(fmtp, "pcurrent", c->pcurrent);
			kdb_printf(fmtl, "data_offset", c->data_offset);
			kdb_printf(fmtl, "kernelstack", c->kernelstack);
			kdb_printf(fmtl, "oldrsp", c->oldrsp);
			kdb_printf(fmtd, "irqcount", c->irqcount);
			kdb_printf(fmtd, "cpunumber", c->cpunumber);
			kdb_printf(fmtp, "irqstackptr", c->irqstackptr);
			kdb_printf(fmtd, "nodenumber", c->nodenumber);
			kdb_printf(fmtd, "__softirq_pending", c->__softirq_pending);
			kdb_printf(fmtd, "__nmi_count", c->__nmi_count);
			kdb_printf(fmtd, "mmu_state", c->mmu_state);
			kdb_printf(fmtp, "active_mm", c->active_mm);
			kdb_printf(fmtd, "apic_timer_irqs", c->apic_timer_irqs);
		}
		if (!all_cpus)
			break;
	}
	return 0;
}

/*
 * kdba_entry
 *
 *	This is the interface routine between
 *	the notifier die_chain and kdb
 */
static int kdba_entry( struct notifier_block *b, unsigned long val, void *v)
{
	struct die_args *args = v;
	int err, trap, ret = 0;
	struct pt_regs *regs;

	regs = args->regs;
	err  = args->err;
	trap  = args->trapnr;
	switch (val){
#ifdef	CONFIG_SMP
		case DIE_NMI_IPI:
			ret = kdb_ipi(regs, NULL);
			break;
#endif	/* CONFIG_SMP */
		case DIE_OOPS:
			ret = kdb(KDB_REASON_OOPS, err, regs);
			break;
		case DIE_CALL:
			ret = kdb(KDB_REASON_ENTER, err, regs);
			break;
		case DIE_DEBUG:
			ret = kdb(KDB_REASON_DEBUG, err, regs);
			break;
		case DIE_NMIWATCHDOG:
			ret = kdb(KDB_REASON_NMI, err, regs);
			break;
		case DIE_INT3:
			 ret = kdb(KDB_REASON_BREAK, err, regs);
			// falls thru
		default:
			break;
	}
	return (ret ? NOTIFY_STOP : NOTIFY_DONE);
}

/*
 * notifier block for kdb entry
 */
static struct notifier_block kdba_notifier = {
	.notifier_call = kdba_entry
};

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
	kdb_register("cpu_pda", kdba_cpu_pda, "<cpu>", "Format struct cpu_pda", 0);
	register_die_notifier(&kdba_notifier);
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
 *	ef		The exception frame at time of fault/breakpoint.  If reason
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
kdba_adjust_ip(kdb_reason_t reason, int error, struct pt_regs *ef)
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

#ifdef	CONFIG_SMP

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
extern asmlinkage void smp_kdb_interrupt(struct pt_regs *regs);	/* for sparse */
asmlinkage void
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
