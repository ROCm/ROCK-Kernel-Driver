/*
 * Kernel Debugger Architecture Independent Support Functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/rse.h>
#include <asm/delay.h>
#ifdef CONFIG_SMP
#include <asm/hw_irq.h>
#endif

static int
kdba_itm (int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	unsigned long val;

	diag = kdbgetularg(argv[1], &val);
	if (diag)
		return diag;
	kdb_printf("new itm=" kdb_machreg_fmt "\n", val);

	ia64_set_itm(val);
	return 0;
}

static void
kdba_show_intregs(void)
{
	u64 lid, tpr, lrr0, lrr1, itv, pmv, cmcv;

	asm ("mov %0=cr.lid" : "=r"(lid));
	asm ("mov %0=cr.tpr" : "=r"(tpr));
	asm ("mov %0=cr.lrr0" : "=r"(lrr0));
	asm ("mov %0=cr.lrr1" : "=r"(lrr1));
	kdb_printf("lid=" kdb_machreg_fmt ", tpr=" kdb_machreg_fmt ", lrr0=" kdb_machreg_fmt ", llr1=" kdb_machreg_fmt "\n", lid, tpr, lrr0, lrr1);

	asm ("mov %0=cr.itv" : "=r"(itv));
	asm ("mov %0=cr.pmv" : "=r"(pmv));
	asm ("mov %0=cr.cmcv" : "=r"(cmcv));
	kdb_printf("itv=" kdb_machreg_fmt ", pmv=" kdb_machreg_fmt ", cmcv=" kdb_machreg_fmt "\n", itv, pmv, cmcv);

	kdb_printf("irr=0x%016lx,0x%016lx,0x%016lx,0x%016lx\n",
		ia64_getreg(_IA64_REG_CR_IRR0), ia64_getreg(_IA64_REG_CR_IRR1), ia64_getreg(_IA64_REG_CR_IRR2), ia64_getreg(_IA64_REG_CR_IRR3));

	kdb_printf("itc=0x%016lx, itm=0x%016lx\n", ia64_get_itc(), ia64_get_itm());
}

static int
kdba_sir (int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	kdba_show_intregs();

	return 0;
}

/*
 * kdba_pt_regs
 *
 *	Format a struct pt_regs
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	If no address is supplied, it uses regs.
 */

static int
kdba_pt_regs(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	kdb_machreg_t addr;
	long offset = 0;
	int nextarg;
	struct pt_regs *p;

	if (argc == 0) {
		addr = (kdb_machreg_t) regs;
	} else if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
		if (diag)
			return diag;
	} else {
		return KDB_ARGCOUNT;
	}

	p = (struct pt_regs *) addr;
	kdb_printf("struct pt_regs %p-%p\n", p, (unsigned char *)p + sizeof(*p) - 1);
	kdb_print_nameval("b6", p->b6);
	kdb_print_nameval("b7", p->b7);
	kdb_printf("  ar_csd      0x%lx\n", p->ar_csd);
	kdb_printf("  ar_ssd      0x%lx\n", p->ar_ssd);
	kdb_print_nameval("r8", p->r8);
	kdb_print_nameval("r9", p->r9);
	kdb_print_nameval("r10", p->r10);
	kdb_print_nameval("r11", p->r11);
	kdb_printf("  cr_ipsr     0x%lx\n", p->cr_ipsr);
	kdb_print_nameval("cr_iip", p->cr_iip);
	kdb_printf("  cr_ifs      0x%lx\n", p->cr_ifs);
	kdb_printf("  ar_unat     0x%lx\n", p->ar_unat);
	kdb_printf("  ar_pfs      0x%lx\n", p->ar_pfs);
	kdb_printf("  ar_rsc      0x%lx\n", p->ar_rsc);
	kdb_printf("  ar_rnat     0x%lx\n", p->ar_rnat);
	kdb_printf("  ar_bspstore 0x%lx\n", p->ar_bspstore);
	kdb_printf("  pr          0x%lx\n", p->pr);
	kdb_print_nameval("b0", p->b0);
	kdb_printf("  loadrs      0x%lx\n", p->loadrs);
	kdb_print_nameval("r1", p->r1);
	kdb_print_nameval("r12", p->r12);
	kdb_print_nameval("r13", p->r13);
	kdb_printf("  ar_fpsr     0x%lx\n", p->ar_fpsr);
	kdb_print_nameval("r15", p->r15);
	kdb_print_nameval("r14", p->r14);
	kdb_print_nameval("r2", p->r2);
	kdb_print_nameval("r3", p->r3);
	kdb_print_nameval("r16", p->r16);
	kdb_print_nameval("r17", p->r17);
	kdb_print_nameval("r18", p->r18);
	kdb_print_nameval("r19", p->r19);
	kdb_print_nameval("r20", p->r20);
	kdb_print_nameval("r21", p->r21);
	kdb_print_nameval("r22", p->r22);
	kdb_print_nameval("r23", p->r23);
	kdb_print_nameval("r24", p->r24);
	kdb_print_nameval("r25", p->r25);
	kdb_print_nameval("r26", p->r26);
	kdb_print_nameval("r27", p->r27);
	kdb_print_nameval("r28", p->r28);
	kdb_print_nameval("r29", p->r29);
	kdb_print_nameval("r30", p->r30);
	kdb_print_nameval("r31", p->r31);
	kdb_printf("  ar_ccv      0x%lx\n", p->ar_ccv);
	kdb_printf("  f6          0x%lx 0x%lx\n", p->f6.u.bits[0], p->f6.u.bits[1]);
	kdb_printf("  f7          0x%lx 0x%lx\n", p->f7.u.bits[0], p->f7.u.bits[1]);
	kdb_printf("  f8          0x%lx 0x%lx\n", p->f8.u.bits[0], p->f8.u.bits[1]);
	kdb_printf("  f9          0x%lx 0x%lx\n", p->f9.u.bits[0], p->f9.u.bits[1]);
	kdb_printf("  f10         0x%lx 0x%lx\n", p->f10.u.bits[0], p->f10.u.bits[1]);
	kdb_printf("  f11         0x%lx 0x%lx\n", p->f11.u.bits[0], p->f11.u.bits[1]);

	return 0;
}

/*
 * kdb_switch_stack
 *
 *	Format a struct switch_stack
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	If no address is supplied, it uses kdb_running_process[smp_processor_id()].arch.sw.
 */

static int
kdba_switch_stack(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	kdb_machreg_t addr;
	long offset = 0;
	int nextarg;
	struct switch_stack *p;

	if (argc == 0) {
		addr = (kdb_machreg_t) kdb_running_process[smp_processor_id()].arch.sw;
	} else if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
		if (diag)
			return diag;
	} else {
		return KDB_ARGCOUNT;
	}

	p = (struct switch_stack *) addr;
	kdb_printf("struct switch_stack %p-%p\n", p, (unsigned char *)p + sizeof(*p) - 1);
	kdb_printf("  caller_unat 0x%lx\n", p->caller_unat);
	kdb_printf("  ar_fpsr     0x%lx\n", p->ar_fpsr);
	kdb_printf("  f2          0x%lx 0x%lx\n", p->f2.u.bits[0], p->f2.u.bits[1]);
	kdb_printf("  f3          0x%lx 0x%lx\n", p->f3.u.bits[0], p->f3.u.bits[1]);
	kdb_printf("  f4          0x%lx 0x%lx\n", p->f4.u.bits[0], p->f4.u.bits[1]);
	kdb_printf("  f5          0x%lx 0x%lx\n", p->f5.u.bits[0], p->f5.u.bits[1]);
	kdb_printf("  f12         0x%lx 0x%lx\n", p->f12.u.bits[0], p->f12.u.bits[1]);
	kdb_printf("  f13         0x%lx 0x%lx\n", p->f13.u.bits[0], p->f13.u.bits[1]);
	kdb_printf("  f14         0x%lx 0x%lx\n", p->f14.u.bits[0], p->f14.u.bits[1]);
	kdb_printf("  f15         0x%lx 0x%lx\n", p->f15.u.bits[0], p->f15.u.bits[1]);
	kdb_printf("  f16         0x%lx 0x%lx\n", p->f16.u.bits[0], p->f16.u.bits[1]);
	kdb_printf("  f17         0x%lx 0x%lx\n", p->f17.u.bits[0], p->f17.u.bits[1]);
	kdb_printf("  f18         0x%lx 0x%lx\n", p->f18.u.bits[0], p->f18.u.bits[1]);
	kdb_printf("  f19         0x%lx 0x%lx\n", p->f19.u.bits[0], p->f19.u.bits[1]);
	kdb_printf("  f20         0x%lx 0x%lx\n", p->f20.u.bits[0], p->f20.u.bits[1]);
	kdb_printf("  f21         0x%lx 0x%lx\n", p->f21.u.bits[0], p->f21.u.bits[1]);
	kdb_printf("  f22         0x%lx 0x%lx\n", p->f22.u.bits[0], p->f22.u.bits[1]);
	kdb_printf("  f23         0x%lx 0x%lx\n", p->f23.u.bits[0], p->f23.u.bits[1]);
	kdb_printf("  f24         0x%lx 0x%lx\n", p->f24.u.bits[0], p->f24.u.bits[1]);
	kdb_printf("  f25         0x%lx 0x%lx\n", p->f25.u.bits[0], p->f25.u.bits[1]);
	kdb_printf("  f26         0x%lx 0x%lx\n", p->f26.u.bits[0], p->f26.u.bits[1]);
	kdb_printf("  f27         0x%lx 0x%lx\n", p->f27.u.bits[0], p->f27.u.bits[1]);
	kdb_printf("  f28         0x%lx 0x%lx\n", p->f28.u.bits[0], p->f28.u.bits[1]);
	kdb_printf("  f29         0x%lx 0x%lx\n", p->f29.u.bits[0], p->f29.u.bits[1]);
	kdb_printf("  f30         0x%lx 0x%lx\n", p->f30.u.bits[0], p->f30.u.bits[1]);
	kdb_printf("  f31         0x%lx 0x%lx\n", p->f31.u.bits[0], p->f31.u.bits[1]);
	kdb_print_nameval("r4", p->r4);
	kdb_print_nameval("r5", p->r5);
	kdb_print_nameval("r6", p->r6);
	kdb_print_nameval("r7", p->r7);
	kdb_print_nameval("b0", p->b0);
	kdb_print_nameval("b1", p->b1);
	kdb_print_nameval("b2", p->b2);
	kdb_print_nameval("b3", p->b3);
	kdb_print_nameval("b4", p->b4);
	kdb_print_nameval("b5", p->b5);
	kdb_printf("  ar_pfs      0x%lx\n", p->ar_pfs);
	kdb_printf("  ar_lc       0x%lx\n", p->ar_lc);
	kdb_printf("  ar_unat     0x%lx\n", p->ar_unat);
	kdb_printf("  ar_rnat     0x%lx\n", p->ar_rnat);
	kdb_printf("  ar_bspstore 0x%lx\n", p->ar_bspstore);
	kdb_printf("  pr          0x%lx\n", p->pr);

	return 0;
}

void
kdba_installdbreg(kdb_bp_t *bp)
{
	/* FIXME: code this */
}

void
kdba_removedbreg(kdb_bp_t *bp)
{
	/* FIXME: code this */
}

static kdb_machreg_t
kdba_getdr(int regnum)
{
	kdb_machreg_t contents = 0;
	unsigned long reg = (unsigned long)regnum;

	__asm__ ("mov %0=ibr[%1]"::"r"(contents),"r"(reg));
//        __asm__ ("mov ibr[%0]=%1"::"r"(dbreg_cond),"r"(value));

	return contents;
}


static void
get_fault_regs(fault_regs_t *fr)
{
	fr->ifa = 0 ;
	fr->isr = 0 ;

	__asm__ ("rsm psr.ic;;") ;
	ia64_srlz_d();
	__asm__ ("mov %0=cr.ifa" : "=r"(fr->ifa));
	__asm__ ("mov %0=cr.isr" : "=r"(fr->isr));
	__asm__ ("ssm psr.ic;;") ;
	ia64_srlz_d();
}

static void
show_kernel_regs (void)
{
	unsigned long kr[8];
	int i;

	asm ("mov %0=ar.k0" : "=r"(kr[0])); asm ("mov %0=ar.k1" : "=r"(kr[1]));
	asm ("mov %0=ar.k2" : "=r"(kr[2])); asm ("mov %0=ar.k3" : "=r"(kr[3]));
	asm ("mov %0=ar.k4" : "=r"(kr[4])); asm ("mov %0=ar.k5" : "=r"(kr[5]));
	asm ("mov %0=ar.k6" : "=r"(kr[6])); asm ("mov %0=ar.k7" : "=r"(kr[7]));

	for (i = 0; i < 4; ++i)
		kdb_printf(" kr%d: %016lx  kr%d: %016lx\n", 2*i, kr[2*i], 2*i+1, kr[2*i+1]);
	kdb_printf("\n");
}

static int
change_cur_stack_frame(struct pt_regs *regs, int regno, unsigned long *contents)
{
	unsigned long sof, i, cfm, sp, *bsp;
	struct unw_frame_info info;
	mm_segment_t old_fs;

	unw_init_frame_info(&info, current, kdb_running_process[smp_processor_id()].arch.sw);
	do {
		if (unw_unwind(&info) < 0) {
			kdb_printf("Failed to unwind\n");
			return 0;
		}
		unw_get_sp(&info, &sp);
	} while (sp <= (unsigned long) regs);
	unw_get_bsp(&info, (unsigned long *) &bsp);
	unw_get_cfm(&info, &cfm);

	if (!bsp) {
		kdb_printf("Unable to get Current Stack Frame\n");
		return 0;
	}

	sof = (cfm & 0x7f);

	if(((unsigned long)regno - 32) >= (sof - 2)) return 1;

	old_fs = set_fs(KERNEL_DS);
	{
		for (i = 0; i < (regno - 32); ++i) {
			bsp = ia64_rse_skip_regs(bsp, 1);
		}
		put_user(*contents, bsp);
	}
	set_fs(old_fs);

	return 0 ;
}

static int
show_cur_stack_frame(struct pt_regs *regs, int regno, unsigned long *contents)
{
	unsigned long sof, i, cfm, val, sp, *bsp;
	struct unw_frame_info info;
	mm_segment_t old_fs;

	/* XXX It would be better to simply create a copy of an unw_frame_info structure
	 * that is set up in kdba_main_loop().  That way, we could avoid having to skip
	 * over the first few frames every time...
	 */
	unw_init_frame_info(&info, current, kdb_running_process[smp_processor_id()].arch.sw);
	do {
		if (unw_unwind(&info) < 0) {
			kdb_printf("Failed to unwind\n");
			return 0;
		}
		unw_get_sp(&info, &sp);
	} while (sp <= (unsigned long) regs);
	unw_get_bsp(&info, (unsigned long *) &bsp);
	unw_get_cfm(&info, &cfm);

	if (!bsp) {
		kdb_printf("Unable to display Current Stack Frame\n");
		return 0;
	}

	sof = (cfm & 0x7f);

	if (regno) {
		if ((unsigned) regno - 32 >= sof)
			return 0;
		bsp = ia64_rse_skip_regs(bsp, regno - 32);
		old_fs = set_fs(KERNEL_DS);
		{
			get_user(val, bsp);
		}
		set_fs(old_fs);
		*contents = val;
		return 1;
	}

	old_fs = set_fs(KERNEL_DS);
	{
		for (i = 0; i < sof; ++i) {
			get_user(val, bsp);
			kdb_printf(" r%lu: %016lx ", 32 + i, val);
			if (!((i + 1) % 3))
				kdb_printf("\n");
			bsp = ia64_rse_skip_regs(bsp, 1);
		}
		kdb_printf("\n");
	}
	set_fs(old_fs);

	return 0 ;
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
 * 	   sstk		 - Prints switch stack for ia64
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
 *
 * 	Note that this function is really machine independent.   The kdb
 *	register list is not, however.
 */

static struct kdbregs {
	char   *reg_name;
	size_t	reg_offset;
} kdbreglist[] = {
	{ "psr",	offsetof(struct pt_regs, cr_ipsr) },
	{ "ifs",	offsetof(struct pt_regs, cr_ifs) },
	{ "ip",	offsetof(struct pt_regs, cr_iip) },

	{ "unat", 	offsetof(struct pt_regs, ar_unat) },
	{ "pfs",	offsetof(struct pt_regs, ar_pfs) },
	{ "rsc", 	offsetof(struct pt_regs, ar_rsc) },

	{ "rnat",	offsetof(struct pt_regs, ar_rnat) },
	{ "bsps",	offsetof(struct pt_regs, ar_bspstore) },
	{ "pr",	offsetof(struct pt_regs, pr) },

	{ "ldrs",	offsetof(struct pt_regs, loadrs) },
	{ "ccv",	offsetof(struct pt_regs, ar_ccv) },
	{ "fpsr",	offsetof(struct pt_regs, ar_fpsr) },

	{ "b0",	offsetof(struct pt_regs, b0) },
	{ "b6",	offsetof(struct pt_regs, b6) },
	{ "b7",	offsetof(struct pt_regs, b7) },

	{ "r1",offsetof(struct pt_regs, r1) },
	{ "r2",offsetof(struct pt_regs, r2) },
	{ "r3",offsetof(struct pt_regs, r3) },

	{ "r8",offsetof(struct pt_regs, r8) },
	{ "r9",offsetof(struct pt_regs, r9) },
	{ "r10",offsetof(struct pt_regs, r10) },

	{ "r11",offsetof(struct pt_regs, r11) },
	{ "r12",offsetof(struct pt_regs, r12) },
	{ "r13",offsetof(struct pt_regs, r13) },

	{ "r14",offsetof(struct pt_regs, r14) },
	{ "r15",offsetof(struct pt_regs, r15) },
	{ "r16",offsetof(struct pt_regs, r16) },

	{ "r17",offsetof(struct pt_regs, r17) },
	{ "r18",offsetof(struct pt_regs, r18) },
	{ "r19",offsetof(struct pt_regs, r19) },

	{ "r20",offsetof(struct pt_regs, r20) },
	{ "r21",offsetof(struct pt_regs, r21) },
	{ "r22",offsetof(struct pt_regs, r22) },

	{ "r23",offsetof(struct pt_regs, r23) },
	{ "r24",offsetof(struct pt_regs, r24) },
	{ "r25",offsetof(struct pt_regs, r25) },

	{ "r26",offsetof(struct pt_regs, r26) },
	{ "r27",offsetof(struct pt_regs, r27) },
	{ "r28",offsetof(struct pt_regs, r28) },

	{ "r29",offsetof(struct pt_regs, r29) },
	{ "r30",offsetof(struct pt_regs, r30) },
	{ "r31",offsetof(struct pt_regs, r31) },

};

static const int nkdbreglist = sizeof(kdbreglist) / sizeof(struct kdbregs);

int
kdba_getregcontents(const char *regname, struct pt_regs *regs, unsigned long *contents)
{
	int i;

	if (strcmp(regname, "isr") == 0) {
		fault_regs_t fr ;
		get_fault_regs(&fr) ;
		*contents = fr.isr ;
		return 0 ;
	}

	if (!regs) {
		kdb_printf("%s: pt_regs not available\n", __FUNCTION__);
		return KDB_BADREG;
	}

	if (strcmp(regname, "&regs") == 0) {
		*contents = (unsigned long)regs;
		return 0;
	}

	if (strcmp(regname, "sstk") == 0) {
		*contents = (unsigned long)getprsregs(regs) ;
		return 0;
	}

	if (strcmp(regname, "ksp") == 0) {
		*contents = (unsigned long) (regs + 1);
		return 0;
	}

	for (i=0; i<nkdbreglist; i++) {
		if (strstr(kdbreglist[i].reg_name, regname))
			break;
	}

	if (i == nkdbreglist) {
		/* Lets check the rse maybe */
		if (regname[0] == 'r')
			if (show_cur_stack_frame(regs, simple_strtoul(regname+1, 0, 0), contents))
				return 0 ;
		return KDB_BADREG;
	}

	*contents = *(unsigned long *)((unsigned long)regs +
			kdbreglist[i].reg_offset);

	return 0;
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
	int i, ret = 0, fixed = 0;
	char *endp;
	unsigned long regno;

	if (regname[0] == '%') {
		regname++;
		regs = (struct pt_regs *)
			(kdb_current_task->thread.ksp - sizeof(struct pt_regs));
	}

	if (!regs) {
		kdb_printf("%s: pt_regs not available\n", __FUNCTION__);
		return KDB_BADREG;
	}

	/* fixed registers */
	for (i=0; i<nkdbreglist; i++) {
		if (strnicmp(kdbreglist[i].reg_name,
			     regname,
			     strlen(regname)) == 0) {
			fixed = 1;
			break;
		}
	}

	/* stacked registers */
	if (!fixed) {
		regno = (simple_strtoul(&regname[1], &endp, 10));
		if ((regname[0] == 'r') && regno > (unsigned long)31) {
			ret = change_cur_stack_frame(regs, regno, &contents);
			if(!ret) return 0;
		}
	}

	if ((i == nkdbreglist)
	    || (strlen(kdbreglist[i].reg_name) != strlen(regname))
	    || ret) {
		return KDB_BADREG;
	}

	/* just in case of "standard" register */
	*(unsigned long *)((unsigned long)regs + kdbreglist[i].reg_offset) =
		contents;

	return 0;
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
 *	d 		Debug registers
 *	c		Control registers
 *	u		User registers at most recent entry to kernel
 *	i		Interrupt registers -- same as "irr" command
 * Following not yet implemented:
 *	m		Model Specific Registers (extra defines register #)
 *	r		Memory Type Range Registers (extra defines register)
 *
 *	For now, all registers are covered as follows:
 *
 * 	rd 		- dumps all regs
 *	rd	%isr	- current interrupt status reg, read freshly
 *	rd	s	- valid stacked regs
 * 	rd 	%sstk	- gets switch stack addr. dump memory and search
 *	rd	d	- debug regs, may not be too useful
 *	rd	k	- dump kernel regs
 *
 *	ARs		TB Done
 *	OTHERS		TB Decided ??
 *
 *	Intel wish list
 *	These will be implemented later - Srinivasa
 *
 *      type        action
 *      ----        ------
 *      g           dump all General static registers
 *      s           dump all general Stacked registers
 *      f           dump all Floating Point registers
 *      p           dump all Predicate registers
 *      b           dump all Branch registers
 *      a           dump all Application registers
 *      c           dump all Control registers
 *
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
			(kdb_current_task->thread.ksp - sizeof(struct pt_regs));
	}

	if (type == NULL) {
		if (!regs) {
			kdb_printf("%s: pt_regs not available\n", __FUNCTION__);
			return KDB_BADREG;
		}
		for (i=0; i<nkdbreglist; i++) {
			kdb_printf("%4s: 0x%16.16lx  ",
				   kdbreglist[i].reg_name,
				   *(unsigned long *)((unsigned long)regs +
						  kdbreglist[i].reg_offset));

			if ((++count % 3) == 0)
				kdb_printf("\n");
		}

		kdb_printf("&regs = %p\n", (void *)regs);

		return 0;
	}

	switch (type[0]) {
	case 'd':
	{
		for(i=0; i<8; i+=2) {
			kdb_printf("idr%d: 0x%16.16lx  idr%d: 0x%16.16lx\n", i,
					kdba_getdr(i), i+1, kdba_getdr(i+1));

		}
		return 0;
	}
	case 'i':
		kdba_show_intregs();
		break;
	case 'k':
		show_kernel_regs();
		break;
	case 'm':
		break;
	case 'r':
		break;

	case 's':
	{
		if (!regs) {
			kdb_printf("%s: pt_regs not available\n", __FUNCTION__);
			return KDB_BADREG;
		}
		show_cur_stack_frame(regs, 0, NULL) ;

		return 0 ;
	}

	case '%':
	{
		unsigned long contents ;

		if (!kdba_getregcontents(type+1, regs, &contents))
			kdb_printf("%s = 0x%16.16lx\n", type+1, contents) ;
		else
			kdb_printf("diag: Invalid register %s\n", type+1) ;

		return 0 ;
	}

	default:
		return KDB_BADREG;
	}

	/* NOTREACHED */
	return 0;
}

kdb_machreg_t
kdba_getpc(struct pt_regs *regs)
{
	return regs ? regs->cr_iip + ia64_psr(regs)->ri : 0;
}

int
kdba_setpc(struct pt_regs *regs, kdb_machreg_t newpc)
{
	if (KDB_NULL_REGS(regs))
		return KDB_BADREG;
	regs->cr_iip = newpc & ~0xf;
	ia64_psr(regs)->ri = newpc & 0x3;
	KDB_STATE_SET(IP_ADJUSTED);
	return 0;
}

struct kdba_main_loop_data {
	kdb_reason_t reason;
	kdb_reason_t reason2;
	int error;
	kdb_dbtrap_t db_result;
	struct pt_regs *regs;
	int ret;
};

/*
 * kdba_sw_interrupt
 *	Invoked from interrupt wrappers via unw_init_running() after that
 *	routine has pushed a struct switch_stack.  The interrupt wrappers
 *	go around functions that disappear into sal/pal and therefore cannot
 *	get into kdb.  This routine saves the switch stack information so
 *	kdb can get diagnostics for such routines, even though they are not
 *	blocked in the kernel.
 *
 * Inputs:
 *	info	Unwind information.
 *	data	structure passed as void * to unw_init_running.  It identifies
 *		the real interrupt handler and contains its parameters.
 * Returns:
 *	none (unw_init_running requires void).
 * Outputs:
 *	none
 * Locking:
 *	None.
 * Remarks:
 *	unw_init_running() creates struct switch_stack then struct
 *	unw_frame_info.  We get the address of the info so step over
 *	that to get switch_stack.  Just hope that unw_init_running
 *	does not change its stack usage.  unw_init_running adds padding
 *	to put switch_stack on a 16 byte boundary.
 */

KDBA_UNWIND_HANDLER(kdba_sw_interrupt, struct kdba_sw_interrupt_data, data->irq,
	data->ret = (data->handler)(data->irq, data->arg, data->regs)
	);

/*
 * do_kdba_main_loop
 *
 *	Invoked from kdba_main_loop via unw_init_running() after that routine
 *	has pushed a struct switch_stack.
 *
 * Inputs:
 *	info	Unwind information.
 *	data	kdb data passed as void * to unw_init_running.
 * Returns:
 *	none (unw_init_running requires void).  vdata->ret is set to
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Outputs:
 *	none
 * Locking:
 *	None.
 * Remarks:
 *	As for kdba_sw_interrupt above.
 */

static KDBA_UNWIND_HANDLER(do_kdba_main_loop, struct kdba_main_loop_data, data->reason,
	data->ret = kdb_main_loop(data->reason, data->reason2, data->error, data->db_result, data->regs));

/*
 * kdba_main_loop
 *
 *	Do any architecture specific set up before entering the main kdb loop.
 *	The primary function of this routine is to make all processes look the
 *	same to kdb, kdb must be able to list a process without worrying if the
 *	process is running or blocked, so make all processes look as though they
 *	are blocked.
 *
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	error2		kdb's current reason code.  Initially error but can change
 *			acording to kdb state.
 *	db_result	Result from break or debug point.
 *	regs		The exception frame at time of fault/breakpoint.  If reason
 *			is KDB_REASON_SILENT then regs is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 * Outputs:
 *	Builds a switch_stack structure before calling the main loop.
 * Locking:
 *	None.
 * Remarks:
 *	none.
 */

int
kdba_main_loop(kdb_reason_t reason, kdb_reason_t reason2, int error,
	       kdb_dbtrap_t db_result, struct pt_regs *regs)
{
	struct kdba_main_loop_data data;
	KDB_DEBUG_STATE("kdba_main_loop", reason);
	data.reason = reason;
	data.reason2 = reason2;
	data.error = error;
	data.db_result = db_result;
	data.regs = regs;
	if (reason == KDB_REASON_CALL_PRESET) {
		/* kdb_running_process has been preset, do not overwrite it */
		KDB_DEBUG_STATE(__FUNCTION__, KDB_REASON_CALL_PRESET);
		data.ret = kdb_main_loop(KDB_REASON_CALL, KDB_REASON_CALL, data.error, data.db_result, data.regs);
	}
	else
		unw_init_running(do_kdba_main_loop, &data);
	return(data.ret);
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
	ia64_psr(regs)->ss = 1;
}

void
kdba_clearsinglestep(struct pt_regs *regs)
{
	if (KDB_NULL_REGS(regs))
		return;
	ia64_psr(regs)->ss = 0;
}

/*
 * kdba_enable_lbr
 *
 *	Enable last branch recording.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None
 * Locking:
 *	None
 * Remarks:
 *	None.
 */

void
kdba_enable_lbr(void)
{
}

/*
 * kdba_disable_lbr
 *
 *	disable last branch recording.
 *
 * Parameters:
 *	None.
 * Returns:
 *	None
 * Locking:
 *	None
 * Remarks:
 *	None.
 */

void
kdba_disable_lbr(void)
{
}

/*
 * kdba_print_lbr
 *
 *	Print last branch and last exception addresses
 *
 * Parameters:
 *	None.
 * Returns:
 *	None
 * Locking:
 *	None
 * Remarks:
 *	None.
 */

void
kdba_print_lbr(void)
{
}
/*
 * kdb_tpa
 *
 * 	Virtual to Physical address translation command.
 *
 *	tpa  <addr>
 *
 * Parameters:
 *	argc	Count of arguments in argv
 *	argv	Space delimited command line arguments
 *	envp	Environment value
 *	regs	Exception frame at entry to kernel debugger
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Locking:
 *	None.
 * Remarks:
 */
#define __xtpa(x)		({ia64_va _v; asm("tpa %0=%1" : "=r"(_v.l) : "r"(x)); _v.l;})
static int
kdba_tpa(int argc, const char **argv, const char **envp, struct pt_regs* regs)
{
	kdb_machreg_t addr;
	int diag;
	long offset = 0;
	int nextarg;
	char c;

	nextarg = 1;
	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;
	if (kdb_getarea(c, addr))
		return(0);
	kdb_printf("vaddr: 0x%lx , paddr: 0x%lx\n", addr, __xtpa(addr));
	return(0);
}
#if defined(CONFIG_NUMA)
static int
kdba_tpav(int argc, const char **argv, const char **envp, struct pt_regs* regs)
{
	kdb_machreg_t addr, end, paddr;
	int diag;
	long offset = 0;
	int nextarg, nid, nid_old;
	char c;

	nextarg = 1;
	if (argc != 2)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &end, &offset, NULL, regs);
	if (diag)
		return diag;
	if (kdb_getarea(c, addr))
		return(0);
	if (kdb_getarea(c, end))
		return(0);
	paddr=__xtpa(addr);
	nid = paddr_to_nid(paddr);
	kdb_printf("begin: 0x%lx , paddr: 0x%lx , nid: %d\n", addr, __xtpa(addr), nid);
	for(;addr<end; addr += PAGE_SIZE) {
		nid_old=nid;
		paddr =__xtpa(addr);
		nid = paddr_to_nid(paddr);
		if (nid != nid_old)
			kdb_printf("NOT on same NODE: addr: 0x%lx , paddr: 0x%lx , nid: %d\n", addr, paddr, nid);
	}
	paddr=__xtpa(end);
	nid=paddr_to_nid(end);
	kdb_printf("end: 0x%lx , paddr: 0x%lx , nid: %d\n", end, paddr, nid);
	return(0);
}
#endif

#if defined(CONFIG_SMP)
/*
 * kdba_sendinit
 *
 *      This function implements the 'init' command.
 *
 *      init    [<cpunum>]
 *
 * Inputs:
 *      argc    argument count
 *      argv    argument vector
 *      envp    environment vector
 *      regs    registers at time kdb was entered.
 * Outputs:
 *      None.
 * Returns:
 *      zero for success, a kdb diagnostic if error
 * Locking:
 *      none.
 * Remarks:
 */

static int
kdba_sendinit(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	unsigned long cpunum;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetularg(argv[1], &cpunum);
	if (diag)
		return diag;

	if ((cpunum > NR_CPUS) || !cpu_online(cpunum))
		return KDB_BADCPUNUM;

	platform_send_ipi(cpunum, 0, IA64_IPI_DM_INIT, 0);
	return 0;
}
#endif /* CONFIG_SMP */

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

void
kdba_init(void)
{
	kdba_enable_lbr();
	kdb_register("irr", kdba_sir, "", "Show interrupt registers", 0);
	kdb_register("itm", kdba_itm, "", "Set new ITM value", 0);
#if defined(CONFIG_SMP)
	kdb_register("init", kdba_sendinit, "", "Send INIT to cpu", 0);
#endif
	kdb_register("pt_regs", kdba_pt_regs, "address", "Format struct pt_regs", 0);
	kdb_register("switch_stack", kdba_switch_stack, "address", "Format struct switch_stack", 0);
	kdb_register("tpa", kdba_tpa, "<vaddr>", "Translate virtual to physical address", 0);
#if defined(CONFIG_NUMA)
	kdb_register("tpav", kdba_tpav, "<begin addr> <end addr>", "Verify that physical addresses corresponding to virtual addresses from <begin addr> to <end addr> are in same node", 0);
#endif

#ifdef CONFIG_SERIAL_8250_CONSOLE
	kdba_serial_console = KDBA_SC_STANDARD;
#endif
#ifdef CONFIG_SERIAL_SGI_L1_CONSOLE
	if (ia64_platform_is("sn2"))
		kdba_serial_console = KDBA_SC_SGI_L1;
#endif
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
 *			is KDB_REASON_SILENT then regs is NULL, otherwise it should
 *			always be valid.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	On IA64, KDB_ENTER() uses break which is a fault, not a trap.  The
 *	instruction pointer must be stepped before leaving kdb, otherwise we
 *	get a loop.
 */

void
kdba_adjust_ip(kdb_reason_t reason, int error, struct pt_regs *regs)
{
	if (reason == KDB_REASON_ENTER &&
	    !KDB_STATE(IP_ADJUSTED)) {
		if (KDB_NULL_REGS(regs))
			return;
		if (ia64_psr(regs)->ri < 2)
			kdba_setpc(regs, regs->cr_iip + ia64_psr(regs)->ri + 1);
		else
			kdba_setpc(regs, regs->cr_iip + 16);
	}
}
