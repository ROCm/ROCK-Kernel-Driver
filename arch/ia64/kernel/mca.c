/*
 * File:	mca.c
 * Purpose:	Generic MCA handling layer
 *
 * Updated for latest kernel
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Copyright (C) 2002 Dell Inc.
 * Copyright (C) Matt Domsch (Matt_Domsch@dell.com)
 *
 * Copyright (C) 2002 Intel
 * Copyright (C) Jenna Hall (jenna.s.hall@intel.com)
 *
 * Copyright (C) 2001 Intel
 * Copyright (C) Fred Lewis (frederick.v.lewis@intel.com)
 *
 * Copyright (C) 2000 Intel
 * Copyright (C) Chuck Fleckenstein (cfleck@co.intel.com)
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander(vijay@engr.sgi.com)
 *
 * 03/04/15 D. Mosberger Added INIT backtrace support.
 * 02/03/25 M. Domsch	GUID cleanups
 *
 * 02/01/04 J. Hall	Aligned MCA stack to 16 bytes, added platform vs. CPU
 *			error flag, set SAL default return values, changed
 *			error record structure to linked list, added init call
 *			to sal_get_state_info_size().
 *
 * 01/01/03 F. Lewis    Added setup of CMCI and CPEI IRQs, logging of corrected
 *                      platform errors, completed code for logging of
 *                      corrected & uncorrected machine check errors, and
 *                      updated for conformance with Nov. 2000 revision of the
 *                      SAL 3.0 spec.
 * 00/03/29 C. Fleckenstein  Fixed PAL/SAL update issues, began MCA bug fixes, logging issues,
 *                           added min save state dump, added INIT handler.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kallsyms.h>
#include <linux/smp_lock.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>

#include <asm/delay.h>
#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/sal.h>
#include <asm/mca.h>

#include <asm/irq.h>
#include <asm/hw_irq.h>

#undef MCA_PRT_XTRA_DATA

typedef struct ia64_fptr {
	unsigned long fp;
	unsigned long gp;
} ia64_fptr_t;

ia64_mc_info_t			ia64_mc_info;
ia64_mca_sal_to_os_state_t	ia64_sal_to_os_handoff_state;
ia64_mca_os_to_sal_state_t	ia64_os_to_sal_handoff_state;
u64				ia64_mca_proc_state_dump[512];
u64				ia64_mca_stack[1024] __attribute__((aligned(16)));
u64				ia64_mca_stackframe[32];
u64				ia64_mca_bspstore[1024];
u64				ia64_init_stack[KERNEL_STACK_SIZE/8] __attribute__((aligned(16)));
u64				ia64_mca_sal_data_area[1356];
u64				ia64_tlb_functional;
u64				ia64_os_mca_recovery_successful;
static void			ia64_mca_wakeup_ipi_wait(void);
static void			ia64_mca_wakeup(int cpu);
static void			ia64_mca_wakeup_all(void);
static void			ia64_log_init(int);
extern void			ia64_monarch_init_handler (void);
extern void			ia64_slave_init_handler (void);
extern struct hw_interrupt_type	irq_type_iosapic_level;

static struct irqaction cmci_irqaction = {
	.handler =	ia64_mca_cmc_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"cmc_hndlr"
};

static struct irqaction cmcp_irqaction = {
	.handler =	ia64_mca_cmc_int_caller,
	.flags =	SA_INTERRUPT,
	.name =		"cmc_poll"
};

static struct irqaction mca_rdzv_irqaction = {
	.handler =	ia64_mca_rendez_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"mca_rdzv"
};

static struct irqaction mca_wkup_irqaction = {
	.handler =	ia64_mca_wakeup_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"mca_wkup"
};

#ifdef CONFIG_ACPI
static struct irqaction mca_cpe_irqaction = {
	.handler =	ia64_mca_cpe_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"cpe_hndlr"
};

static struct irqaction mca_cpep_irqaction = {
	.handler =	ia64_mca_cpe_int_caller,
	.flags =	SA_INTERRUPT,
	.name =		"cpe_poll"
};
#endif /* CONFIG_ACPI */

#define MAX_CPE_POLL_INTERVAL (15*60*HZ) /* 15 minutes */
#define MIN_CPE_POLL_INTERVAL (2*60*HZ)  /* 2 minutes */
#define CMC_POLL_INTERVAL     (1*60*HZ)  /* 1 minute */
#define CMC_HISTORY_LENGTH    5

static struct timer_list cpe_poll_timer;
static struct timer_list cmc_poll_timer;
/*
 * This variable tells whether we are currently in polling mode.
 * Start with this in the wrong state so we won't play w/ timers
 * before the system is ready.
 */
static int cmc_polling_enabled = 1;

/*
 * Clearing this variable prevents CPE polling from getting activated
 * in mca_late_init.  Use it if your system doesn't provide a CPEI,
 * but encounters problems retrieving CPE logs.  This should only be
 * necessary for debugging.
 */
static int cpe_poll_enabled = 1;

/*
 *  ia64_mca_log_sal_error_record
 *
 *  This function retrieves a specified error record type from SAL, sends it to
 *  the system log, and notifies SALs to clear the record from its non-volatile
 *  memory.
 *
 *  Inputs  :   sal_info_type   (Type of error record MCA/CMC/CPE/INIT)
 *  Outputs :   platform error status
 */
int
ia64_mca_log_sal_error_record(int sal_info_type, int called_from_init)
{
	int platform_err = 0;

	/* Get the MCA error record */
	if (!ia64_log_get(sal_info_type, (prfunc_t)printk))
		return platform_err;		/* no record retrieved */

	/* TODO:
	 * 1. analyze error logs to determine recoverability
	 * 2. perform error recovery procedures, if applicable
	 * 3. set ia64_os_mca_recovery_successful flag, if applicable
	 */

	platform_err = ia64_log_print(sal_info_type, (prfunc_t)printk);
	/* temporary: only clear SAL logs on hardware-corrected errors
		or if we're logging an error after an MCA-initiated reboot */
	if ((sal_info_type > 1) || (called_from_init))
		ia64_sal_clear_state_info(sal_info_type);

	return platform_err;
}

/*
 * platform dependent error handling
 */
#ifndef PLATFORM_MCA_HANDLERS
void
mca_handler_platform (void)
{

}

irqreturn_t
ia64_mca_cpe_int_handler (int cpe_irq, void *arg, struct pt_regs *ptregs)
{
	IA64_MCA_DEBUG("ia64_mca_cpe_int_handler: received interrupt. CPU:%d vector = %#x\n",
		       smp_processor_id(), cpe_irq);

	/* SAL spec states this should run w/ interrupts enabled */
	local_irq_enable();

	/* Get the CMC error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_CPE, 0);
	return IRQ_HANDLED;
}

static void
show_min_state (pal_min_state_area_t *minstate)
{
	u64 iip = minstate->pmsa_iip + ((struct ia64_psr *)(&minstate->pmsa_ipsr))->ri;
	u64 xip = minstate->pmsa_xip + ((struct ia64_psr *)(&minstate->pmsa_xpsr))->ri;

	printk("NaT bits\t%016lx\n", minstate->pmsa_nat_bits);
	printk("pr\t\t%016lx\n", minstate->pmsa_pr);
	printk("b0\t\t%016lx ", minstate->pmsa_br0); print_symbol("%s\n", minstate->pmsa_br0);
	printk("ar.rsc\t\t%016lx\n", minstate->pmsa_rsc);
	printk("cr.iip\t\t%016lx ", iip); print_symbol("%s\n", iip);
	printk("cr.ipsr\t\t%016lx\n", minstate->pmsa_ipsr);
	printk("cr.ifs\t\t%016lx\n", minstate->pmsa_ifs);
	printk("xip\t\t%016lx ", xip); print_symbol("%s\n", xip);
	printk("xpsr\t\t%016lx\n", minstate->pmsa_xpsr);
	printk("xfs\t\t%016lx\n", minstate->pmsa_xfs);
	printk("b1\t\t%016lx ", minstate->pmsa_br1);
	print_symbol("%s\n", minstate->pmsa_br1);

	printk("\nstatic registers r0-r15:\n");
	printk(" r0- 3 %016lx %016lx %016lx %016lx\n",
	       0UL, minstate->pmsa_gr[0], minstate->pmsa_gr[1], minstate->pmsa_gr[2]);
	printk(" r4- 7 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_gr[3], minstate->pmsa_gr[4],
	       minstate->pmsa_gr[5], minstate->pmsa_gr[6]);
	printk(" r8-11 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_gr[7], minstate->pmsa_gr[8],
	       minstate->pmsa_gr[9], minstate->pmsa_gr[10]);
	printk("r12-15 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_gr[11], minstate->pmsa_gr[12],
	       minstate->pmsa_gr[13], minstate->pmsa_gr[14]);

	printk("\nbank 0:\n");
	printk("r16-19 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[0], minstate->pmsa_bank0_gr[1],
	       minstate->pmsa_bank0_gr[2], minstate->pmsa_bank0_gr[3]);
	printk("r20-23 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[4], minstate->pmsa_bank0_gr[5],
	       minstate->pmsa_bank0_gr[6], minstate->pmsa_bank0_gr[7]);
	printk("r24-27 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[8], minstate->pmsa_bank0_gr[9],
	       minstate->pmsa_bank0_gr[10], minstate->pmsa_bank0_gr[11]);
	printk("r28-31 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[12], minstate->pmsa_bank0_gr[13],
	       minstate->pmsa_bank0_gr[14], minstate->pmsa_bank0_gr[15]);

	printk("\nbank 1:\n");
	printk("r16-19 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[0], minstate->pmsa_bank1_gr[1],
	       minstate->pmsa_bank1_gr[2], minstate->pmsa_bank1_gr[3]);
	printk("r20-23 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[4], minstate->pmsa_bank1_gr[5],
	       minstate->pmsa_bank1_gr[6], minstate->pmsa_bank1_gr[7]);
	printk("r24-27 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[8], minstate->pmsa_bank1_gr[9],
	       minstate->pmsa_bank1_gr[10], minstate->pmsa_bank1_gr[11]);
	printk("r28-31 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[12], minstate->pmsa_bank1_gr[13],
	       minstate->pmsa_bank1_gr[14], minstate->pmsa_bank1_gr[15]);
}

static void
fetch_min_state (pal_min_state_area_t *ms, struct pt_regs *pt, struct switch_stack *sw)
{
	u64 *dst_banked, *src_banked, bit, shift, nat_bits;
	int i;

	/*
	 * First, update the pt-regs and switch-stack structures with the contents stored
	 * in the min-state area:
	 */
	if (((struct ia64_psr *) &ms->pmsa_ipsr)->ic == 0) {
		pt->cr_ipsr = ms->pmsa_xpsr;
		pt->cr_iip = ms->pmsa_xip;
		pt->cr_ifs = ms->pmsa_xfs;
	} else {
		pt->cr_ipsr = ms->pmsa_ipsr;
		pt->cr_iip = ms->pmsa_iip;
		pt->cr_ifs = ms->pmsa_ifs;
	}
	pt->ar_rsc = ms->pmsa_rsc;
	pt->pr = ms->pmsa_pr;
	pt->r1 = ms->pmsa_gr[0];
	pt->r2 = ms->pmsa_gr[1];
	pt->r3 = ms->pmsa_gr[2];
	sw->r4 = ms->pmsa_gr[3];
	sw->r5 = ms->pmsa_gr[4];
	sw->r6 = ms->pmsa_gr[5];
	sw->r7 = ms->pmsa_gr[6];
	pt->r8 = ms->pmsa_gr[7];
	pt->r9 = ms->pmsa_gr[8];
	pt->r10 = ms->pmsa_gr[9];
	pt->r11 = ms->pmsa_gr[10];
	pt->r12 = ms->pmsa_gr[11];
	pt->r13 = ms->pmsa_gr[12];
	pt->r14 = ms->pmsa_gr[13];
	pt->r15 = ms->pmsa_gr[14];
	dst_banked = &pt->r16;		/* r16-r31 are contiguous in struct pt_regs */
	src_banked = ms->pmsa_bank1_gr;
	for (i = 0; i < 16; ++i)
		dst_banked[i] = src_banked[i];
	pt->b0 = ms->pmsa_br0;
	sw->b1 = ms->pmsa_br1;

	/* construct the NaT bits for the pt-regs structure: */
#	define PUT_NAT_BIT(dst, addr)					\
	do {								\
		bit = nat_bits & 1; nat_bits >>= 1;			\
		shift = ((unsigned long) addr >> 3) & 0x3f;		\
		dst = ((dst) & ~(1UL << shift)) | (bit << shift);	\
	} while (0)

	/* Rotate the saved NaT bits such that bit 0 corresponds to pmsa_gr[0]: */
	shift = ((unsigned long) &ms->pmsa_gr[0] >> 3) & 0x3f;
	nat_bits = (ms->pmsa_nat_bits >> shift) | (ms->pmsa_nat_bits << (64 - shift));

	PUT_NAT_BIT(sw->caller_unat, &pt->r1);
	PUT_NAT_BIT(sw->caller_unat, &pt->r2);
	PUT_NAT_BIT(sw->caller_unat, &pt->r3);
	PUT_NAT_BIT(sw->ar_unat, &sw->r4);
	PUT_NAT_BIT(sw->ar_unat, &sw->r5);
	PUT_NAT_BIT(sw->ar_unat, &sw->r6);
	PUT_NAT_BIT(sw->ar_unat, &sw->r7);
	PUT_NAT_BIT(sw->caller_unat, &pt->r8);	PUT_NAT_BIT(sw->caller_unat, &pt->r9);
	PUT_NAT_BIT(sw->caller_unat, &pt->r10);	PUT_NAT_BIT(sw->caller_unat, &pt->r11);
	PUT_NAT_BIT(sw->caller_unat, &pt->r12);	PUT_NAT_BIT(sw->caller_unat, &pt->r13);
	PUT_NAT_BIT(sw->caller_unat, &pt->r14);	PUT_NAT_BIT(sw->caller_unat, &pt->r15);
	nat_bits >>= 16;	/* skip over bank0 NaT bits */
	PUT_NAT_BIT(sw->caller_unat, &pt->r16);	PUT_NAT_BIT(sw->caller_unat, &pt->r17);
	PUT_NAT_BIT(sw->caller_unat, &pt->r18);	PUT_NAT_BIT(sw->caller_unat, &pt->r19);
	PUT_NAT_BIT(sw->caller_unat, &pt->r20);	PUT_NAT_BIT(sw->caller_unat, &pt->r21);
	PUT_NAT_BIT(sw->caller_unat, &pt->r22);	PUT_NAT_BIT(sw->caller_unat, &pt->r23);
	PUT_NAT_BIT(sw->caller_unat, &pt->r24);	PUT_NAT_BIT(sw->caller_unat, &pt->r25);
	PUT_NAT_BIT(sw->caller_unat, &pt->r26);	PUT_NAT_BIT(sw->caller_unat, &pt->r27);
	PUT_NAT_BIT(sw->caller_unat, &pt->r28);	PUT_NAT_BIT(sw->caller_unat, &pt->r29);
	PUT_NAT_BIT(sw->caller_unat, &pt->r30);	PUT_NAT_BIT(sw->caller_unat, &pt->r31);
}

void
init_handler_platform (pal_min_state_area_t *ms,
		       struct pt_regs *pt, struct switch_stack *sw)
{
	struct unw_frame_info info;

	/* if a kernel debugger is available call it here else just dump the registers */

	/*
	 * Wait for a bit.  On some machines (e.g., HP's zx2000 and zx6000, INIT can be
	 * generated via the BMC's command-line interface, but since the console is on the
	 * same serial line, the user will need some time to switch out of the BMC before
	 * the dump begins.
	 */
	printk("Delaying for 5 seconds...\n");
	udelay(5*1000000);
	show_min_state(ms);

	printk("Backtrace of current task (pid %d, %s)\n", current->pid, current->comm);
	fetch_min_state(ms, pt, sw);
	unw_init_from_interruption(&info, current, pt, sw);
	ia64_do_show_stack(&info, NULL);

#ifdef CONFIG_SMP
	/* read_trylock() would be handy... */
	if (!tasklist_lock.write_lock)
		read_lock(&tasklist_lock);
#endif
	{
		struct task_struct *g, *t;
		do_each_thread (g, t) {
			if (t == current)
				continue;

			printk("\nBacktrace of pid %d (%s)\n", t->pid, t->comm);
			show_stack(t, NULL);
		} while_each_thread (g, t);
	}
#ifdef CONFIG_SMP
	if (!tasklist_lock.write_lock)
		read_unlock(&tasklist_lock);
#endif

	printk("\nINIT dump complete.  Please reboot now.\n");
	while (1);			/* hang city if no debugger */
}

/*
 * ia64_mca_init_platform
 *
 *  External entry for platform specific MCA initialization.
 *
 *  Inputs
 *      None
 *
 *  Outputs
 *      None
 */
void
ia64_mca_init_platform (void)
{

}

/*
 *  ia64_mca_check_errors
 *
 *  External entry to check for error records which may have been posted by SAL
 *  for a prior failure which resulted in a machine shutdown before an the
 *  error could be logged.  This function must be called after the filesystem
 *  is initialized.
 *
 *  Inputs  :   None
 *
 *  Outputs :   None
 */
int
ia64_mca_check_errors (void)
{
	/*
	 *  If there is an MCA error record pending, get it and log it.
	 */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_MCA, 1);

	return 0;
}

device_initcall(ia64_mca_check_errors);

#ifdef CONFIG_ACPI
/*
 * ia64_mca_register_cpev
 *
 *  Register the corrected platform error vector with SAL.
 *
 *  Inputs
 *      cpev        Corrected Platform Error Vector number
 *
 *  Outputs
 *      None
 */
static void
ia64_mca_register_cpev (int cpev)
{
	/* Register the CPE interrupt vector with SAL */
	if (ia64_sal_mc_set_params(SAL_MC_PARAM_CPE_INT, SAL_MC_PARAM_MECHANISM_INT, cpev, 0, 0)) {
		printk(KERN_ERR "ia64_mca_platform_init: failed to register Corrected "
		       "Platform Error interrupt vector with SAL.\n");
		return;
	}

	IA64_MCA_DEBUG("ia64_mca_platform_init: corrected platform error "
		       "vector %#x setup and enabled\n", cpev);
}
#endif /* CONFIG_ACPI */

#endif /* PLATFORM_MCA_HANDLERS */

/*
 * ia64_mca_cmc_vector_setup
 *
 *  Setup the corrected machine check vector register in the processor and
 *  unmask interrupt.  This function is invoked on a per-processor basis.
 *
 * Inputs
 *      None
 *
 * Outputs
 *	None
 */
void
ia64_mca_cmc_vector_setup (void)
{
	cmcv_reg_t	cmcv;

	cmcv.cmcv_regval	= 0;
	cmcv.cmcv_mask		= 0;        /* Unmask/enable interrupt */
	cmcv.cmcv_vector	= IA64_CMC_VECTOR;
	ia64_setreg(_IA64_REG_CR_CMCV, cmcv.cmcv_regval);

	IA64_MCA_DEBUG("ia64_mca_platform_init: CPU %d corrected "
		       "machine check vector %#x setup and enabled.\n",
		       smp_processor_id(), IA64_CMC_VECTOR);

	IA64_MCA_DEBUG("ia64_mca_platform_init: CPU %d CMCV = %#016lx\n",
		       smp_processor_id(), ia64_getreg(_IA64_REG_CR_CMCV));
}

/*
 * ia64_mca_cmc_vector_disable
 *
 *  Mask the corrected machine check vector register in the processor.
 *  This function is invoked on a per-processor basis.
 *
 * Inputs
 *      dummy(unused)
 *
 * Outputs
 *	None
 */
void
ia64_mca_cmc_vector_disable (void *dummy)
{
	cmcv_reg_t	cmcv;

	cmcv = (cmcv_reg_t)ia64_getreg(_IA64_REG_CR_CMCV);

	cmcv.cmcv_mask = 1; /* Mask/disable interrupt */
	ia64_setreg(_IA64_REG_CR_CMCV, cmcv.cmcv_regval)

	IA64_MCA_DEBUG("ia64_mca_cmc_vector_disable: CPU %d corrected "
		       "machine check vector %#x disabled.\n",
		       smp_processor_id(), cmcv.cmcv_vector);
}

/*
 * ia64_mca_cmc_vector_enable
 *
 *  Unmask the corrected machine check vector register in the processor.
 *  This function is invoked on a per-processor basis.
 *
 * Inputs
 *      dummy(unused)
 *
 * Outputs
 *	None
 */
void
ia64_mca_cmc_vector_enable (void *dummy)
{
	cmcv_reg_t	cmcv;

	cmcv = (cmcv_reg_t)ia64_getreg(_IA64_REG_CR_CMCV);

	cmcv.cmcv_mask = 0; /* Unmask/enable interrupt */
	ia64_setreg(_IA64_REG_CR_CMCV, cmcv.cmcv_regval)

	IA64_MCA_DEBUG("ia64_mca_cmc_vector_enable: CPU %d corrected "
		       "machine check vector %#x enabled.\n",
		       smp_processor_id(), cmcv.cmcv_vector);
}


#if defined(MCA_TEST)

sal_log_processor_info_t	slpi_buf;

void
mca_test(void)
{
	slpi_buf.valid.psi_static_struct = 1;
	slpi_buf.valid.num_cache_check = 1;
	slpi_buf.valid.num_tlb_check = 1;
	slpi_buf.valid.num_bus_check = 1;
	slpi_buf.valid.processor_static_info.minstate = 1;
	slpi_buf.valid.processor_static_info.br = 1;
	slpi_buf.valid.processor_static_info.cr = 1;
	slpi_buf.valid.processor_static_info.ar = 1;
	slpi_buf.valid.processor_static_info.rr = 1;
	slpi_buf.valid.processor_static_info.fr = 1;

	ia64_os_mca_dispatch();
}

#endif /* #if defined(MCA_TEST) */


/*
 *  verify_guid
 *
 *  Compares a test guid to a target guid and returns result.
 *
 *  Inputs
 *      test_guid *     (ptr to guid to be verified)
 *      target_guid *   (ptr to standard guid to be verified against)
 *
 *  Outputs
 *      0               (test verifies against target)
 *      non-zero        (test guid does not verify)
 */
static int
verify_guid (efi_guid_t *test, efi_guid_t *target)
{
	int     rc;
#ifdef IA64_MCA_DEBUG_INFO
	char out[40];
#endif

	if ((rc = efi_guidcmp(*test, *target))) {
		IA64_MCA_DEBUG(KERN_DEBUG
			       "verify_guid: invalid GUID = %s\n",
			       efi_guid_unparse(test, out));
	}
	return rc;
}

/*
 * ia64_mca_init
 *
 *  Do all the system level mca specific initialization.
 *
 *	1. Register spinloop and wakeup request interrupt vectors
 *
 *	2. Register OS_MCA handler entry point
 *
 *	3. Register OS_INIT handler entry point
 *
 *  4. Initialize MCA/CMC/INIT related log buffers maintained by the OS.
 *
 *  Note that this initialization is done very early before some kernel
 *  services are available.
 *
 *  Inputs  :   None
 *
 *  Outputs :   None
 */
void __init
ia64_mca_init(void)
{
	ia64_fptr_t *mon_init_ptr = (ia64_fptr_t *)ia64_monarch_init_handler;
	ia64_fptr_t *slave_init_ptr = (ia64_fptr_t *)ia64_slave_init_handler;
	ia64_fptr_t *mca_hldlr_ptr = (ia64_fptr_t *)ia64_os_mca_dispatch;
	int i;
	s64 rc;

	IA64_MCA_DEBUG("ia64_mca_init: begin\n");

	/* initialize recovery success indicator */
	ia64_os_mca_recovery_successful = 0;

	/* Clear the Rendez checkin flag for all cpus */
	for(i = 0 ; i < NR_CPUS; i++)
		ia64_mc_info.imi_rendez_checkin[i] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;

	/*
	 * Register the rendezvous spinloop and wakeup mechanism with SAL
	 */

	/* Register the rendezvous interrupt vector with SAL */
	if ((rc = ia64_sal_mc_set_params(SAL_MC_PARAM_RENDEZ_INT,
					 SAL_MC_PARAM_MECHANISM_INT,
					 IA64_MCA_RENDEZ_VECTOR,
					 IA64_MCA_RENDEZ_TIMEOUT,
					 SAL_MC_PARAM_RZ_ALWAYS)))
	{
		printk(KERN_ERR "ia64_mca_init: Failed to register rendezvous interrupt "
		       "with SAL.  rc = %ld\n", rc);
		return;
	}

	/* Register the wakeup interrupt vector with SAL */
	if ((rc = ia64_sal_mc_set_params(SAL_MC_PARAM_RENDEZ_WAKEUP,
					 SAL_MC_PARAM_MECHANISM_INT,
					 IA64_MCA_WAKEUP_VECTOR,
					 0, 0)))
	{
		printk(KERN_ERR "ia64_mca_init: Failed to register wakeup interrupt with SAL.  "
		       "rc = %ld\n", rc);
		return;
	}

	IA64_MCA_DEBUG("ia64_mca_init: registered mca rendezvous spinloop and wakeup mech.\n");

	ia64_mc_info.imi_mca_handler        = ia64_tpa(mca_hldlr_ptr->fp);
	/*
	 * XXX - disable SAL checksum by setting size to 0; should be
	 *	ia64_tpa(ia64_os_mca_dispatch_end) - ia64_tpa(ia64_os_mca_dispatch);
	 */
	ia64_mc_info.imi_mca_handler_size	= 0;

	/* Register the os mca handler with SAL */
	if ((rc = ia64_sal_set_vectors(SAL_VECTOR_OS_MCA,
				       ia64_mc_info.imi_mca_handler,
				       ia64_tpa(mca_hldlr_ptr->gp),
				       ia64_mc_info.imi_mca_handler_size,
				       0, 0, 0)))
	{
		printk(KERN_ERR "ia64_mca_init: Failed to register os mca handler with SAL.  "
		       "rc = %ld\n", rc);
		return;
	}

	IA64_MCA_DEBUG("ia64_mca_init: registered os mca handler with SAL at 0x%lx, gp = 0x%lx\n",
		       ia64_mc_info.imi_mca_handler, ia64_tpa(mca_hldlr_ptr->gp));

	/*
	 * XXX - disable SAL checksum by setting size to 0, should be
	 * IA64_INIT_HANDLER_SIZE
	 */
	ia64_mc_info.imi_monarch_init_handler		= ia64_tpa(mon_init_ptr->fp);
	ia64_mc_info.imi_monarch_init_handler_size	= 0;
	ia64_mc_info.imi_slave_init_handler		= ia64_tpa(slave_init_ptr->fp);
	ia64_mc_info.imi_slave_init_handler_size	= 0;

	IA64_MCA_DEBUG("ia64_mca_init: os init handler at %lx\n",
		       ia64_mc_info.imi_monarch_init_handler);

	/* Register the os init handler with SAL */
	if ((rc = ia64_sal_set_vectors(SAL_VECTOR_OS_INIT,
				       ia64_mc_info.imi_monarch_init_handler,
				       ia64_tpa(ia64_getreg(_IA64_REG_GP)),
				       ia64_mc_info.imi_monarch_init_handler_size,
				       ia64_mc_info.imi_slave_init_handler,
				       ia64_tpa(ia64_getreg(_IA64_REG_GP)),
				       ia64_mc_info.imi_slave_init_handler_size)))
	{
		printk(KERN_ERR "ia64_mca_init: Failed to register m/s init handlers with SAL. "
		       "rc = %ld\n", rc);
		return;
	}

	IA64_MCA_DEBUG("ia64_mca_init: registered os init handler with SAL\n");

	/*
	 *  Configure the CMCI/P vector and handler. Interrupts for CMC are
	 *  per-processor, so AP CMC interrupts are setup in smp_callin() (smpboot.c).
	 */
	register_percpu_irq(IA64_CMC_VECTOR, &cmci_irqaction);
	register_percpu_irq(IA64_CMCP_VECTOR, &cmcp_irqaction);
	ia64_mca_cmc_vector_setup();       /* Setup vector on BSP & enable */

	/* Setup the MCA rendezvous interrupt vector */
	register_percpu_irq(IA64_MCA_RENDEZ_VECTOR, &mca_rdzv_irqaction);

	/* Setup the MCA wakeup interrupt vector */
	register_percpu_irq(IA64_MCA_WAKEUP_VECTOR, &mca_wkup_irqaction);

#ifdef CONFIG_ACPI
	/* Setup the CPE interrupt vector */
	{
		irq_desc_t *desc;
		unsigned int irq;
		int cpev = acpi_request_vector(ACPI_INTERRUPT_CPEI);

		if (cpev >= 0) {
			for (irq = 0; irq < NR_IRQS; ++irq)
				if (irq_to_vector(irq) == cpev) {
					desc = irq_descp(irq);
					desc->status |= IRQ_PER_CPU;
					desc->handler = &irq_type_iosapic_level;
					setup_irq(irq, &mca_cpe_irqaction);
				}
			ia64_mca_register_cpev(cpev);
		}
	}
#endif

	/* Initialize the areas set aside by the OS to buffer the
	 * platform/processor error states for MCA/INIT/CMC
	 * handling.
	 */
	ia64_log_init(SAL_INFO_TYPE_MCA);
	ia64_log_init(SAL_INFO_TYPE_INIT);
	ia64_log_init(SAL_INFO_TYPE_CMC);
	ia64_log_init(SAL_INFO_TYPE_CPE);

#if defined(MCA_TEST)
	mca_test();
#endif /* #if defined(MCA_TEST) */

	printk(KERN_INFO "Mca related initialization done\n");

	/* commented out because this is done elsewhere */
#if 0
	/* Do post-failure MCA error logging */
	ia64_mca_check_errors();
#endif
}

/*
 * ia64_mca_wakeup_ipi_wait
 *
 *	Wait for the inter-cpu interrupt to be sent by the
 *	monarch processor once it is done with handling the
 *	MCA.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
void
ia64_mca_wakeup_ipi_wait(void)
{
	int	irr_num = (IA64_MCA_WAKEUP_VECTOR >> 6);
	int	irr_bit = (IA64_MCA_WAKEUP_VECTOR & 0x3f);
	u64	irr = 0;

	do {
		switch(irr_num) {
		      case 0:
			irr = ia64_getreg(_IA64_REG_CR_IRR0);
			break;
		      case 1:
			irr = ia64_getreg(_IA64_REG_CR_IRR1);
			break;
		      case 2:
			irr = ia64_getreg(_IA64_REG_CR_IRR2);
			break;
		      case 3:
			irr = ia64_getreg(_IA64_REG_CR_IRR3);
			break;
		}
	} while (!(irr & (1UL << irr_bit))) ;
}

/*
 * ia64_mca_wakeup
 *
 *	Send an inter-cpu interrupt to wake-up a particular cpu
 *	and mark that cpu to be out of rendez.
 *
 *  Inputs  :   cpuid
 *  Outputs :   None
 */
void
ia64_mca_wakeup(int cpu)
{
	platform_send_ipi(cpu, IA64_MCA_WAKEUP_VECTOR, IA64_IPI_DM_INT, 0);
	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;

}

/*
 * ia64_mca_wakeup_all
 *
 *	Wakeup all the cpus which have rendez'ed previously.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
void
ia64_mca_wakeup_all(void)
{
	int cpu;

	/* Clear the Rendez checkin flag for all cpus */
	for(cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;
		if (ia64_mc_info.imi_rendez_checkin[cpu] == IA64_MCA_RENDEZ_CHECKIN_DONE)
			ia64_mca_wakeup(cpu);
	}

}

/*
 * ia64_mca_rendez_interrupt_handler
 *
 *	This is handler used to put slave processors into spinloop
 *	while the monarch processor does the mca handling and later
 *	wake each slave up once the monarch is done.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
irqreturn_t
ia64_mca_rendez_int_handler(int rendez_irq, void *arg, struct pt_regs *ptregs)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	/* Mask all interrupts */
	local_irq_save(flags);

	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_DONE;
	/* Register with the SAL monarch that the slave has
	 * reached SAL
	 */
	ia64_sal_mc_rendez();

	/* Wait for the wakeup IPI from the monarch
	 * This waiting is done by polling on the wakeup-interrupt
	 * vector bit in the processor's IRRs
	 */
	ia64_mca_wakeup_ipi_wait();

	/* Enable all interrupts */
	local_irq_restore(flags);
	return IRQ_HANDLED;
}


/*
 * ia64_mca_wakeup_int_handler
 *
 *	The interrupt handler for processing the inter-cpu interrupt to the
 *	slave cpu which was spinning in the rendez loop.
 *	Since this spinning is done by turning off the interrupts and
 *	polling on the wakeup-interrupt bit in the IRR, there is
 *	nothing useful to be done in the handler.
 *
 *  Inputs  :   wakeup_irq  (Wakeup-interrupt bit)
 *	arg		(Interrupt handler specific argument)
 *	ptregs		(Exception frame at the time of the interrupt)
 *  Outputs :   None
 *
 */
irqreturn_t
ia64_mca_wakeup_int_handler(int wakeup_irq, void *arg, struct pt_regs *ptregs)
{
	return IRQ_HANDLED;
}

/*
 * ia64_return_to_sal_check
 *
 *	This is function called before going back from the OS_MCA handler
 *	to the OS_MCA dispatch code which finally takes the control back
 *	to the SAL.
 *	The main purpose of this routine is to setup the OS_MCA to SAL
 *	return state which can be used by the OS_MCA dispatch code
 *	just before going back to SAL.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */

void
ia64_return_to_sal_check(void)
{
	/* Copy over some relevant stuff from the sal_to_os_mca_handoff
	 * so that it can be used at the time of os_mca_to_sal_handoff
	 */
	ia64_os_to_sal_handoff_state.imots_sal_gp =
		ia64_sal_to_os_handoff_state.imsto_sal_gp;

	ia64_os_to_sal_handoff_state.imots_sal_check_ra =
		ia64_sal_to_os_handoff_state.imsto_sal_check_ra;

	/* Cold Boot for uncorrectable MCA */
	ia64_os_to_sal_handoff_state.imots_os_status = IA64_MCA_COLD_BOOT;

	/* Default = tell SAL to return to same context */
	ia64_os_to_sal_handoff_state.imots_context = IA64_MCA_SAME_CONTEXT;

	ia64_os_to_sal_handoff_state.imots_new_min_state =
		(u64 *)ia64_sal_to_os_handoff_state.pal_min_state;
}

/*
 * ia64_mca_ucmc_handler
 *
 *	This is uncorrectable machine check handler called from OS_MCA
 *	dispatch code which is in turn called from SAL_CHECK().
 *	This is the place where the core of OS MCA handling is done.
 *	Right now the logs are extracted and displayed in a well-defined
 *	format. This handler code is supposed to be run only on the
 *	monarch processor. Once the monarch is done with MCA handling
 *	further MCA logging is enabled by clearing logs.
 *	Monarch also has the duty of sending wakeup-IPIs to pull the
 *	slave processors out of rendezvous spinloop.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
void
ia64_mca_ucmc_handler(void)
{
	int platform_err = 0;

	/* Get the MCA error record and log it */
	platform_err = ia64_mca_log_sal_error_record(SAL_INFO_TYPE_MCA, 0);

	/*
	 *  Do Platform-specific mca error handling if required.
	 */
	if (platform_err)
		mca_handler_platform();

	/*
	 *  Wakeup all the processors which are spinning in the rendezvous
	 *  loop.
	 */
	ia64_mca_wakeup_all();

	/* Return to SAL */
	ia64_return_to_sal_check();
}

/*
 * ia64_mca_cmc_int_handler
 *
 *  This is corrected machine check interrupt handler.
 *	Right now the logs are extracted and displayed in a well-defined
 *	format.
 *
 * Inputs
 *      interrupt number
 *      client data arg ptr
 *      saved registers ptr
 *
 * Outputs
 *	None
 */
irqreturn_t
ia64_mca_cmc_int_handler(int cmc_irq, void *arg, struct pt_regs *ptregs)
{
	static unsigned long	cmc_history[CMC_HISTORY_LENGTH];
	static int		index;
	static spinlock_t	cmc_history_lock = SPIN_LOCK_UNLOCKED;

	IA64_MCA_DEBUG("ia64_mca_cmc_int_handler: received interrupt vector = %#x on CPU %d\n",
		       cmc_irq, smp_processor_id());

	/* SAL spec states this should run w/ interrupts enabled */
	local_irq_enable();

	/* Get the CMC error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_CMC, 0);

	spin_lock(&cmc_history_lock);
	if (!cmc_polling_enabled) {
		int i, count = 1; /* we know 1 happened now */
		unsigned long now = jiffies;

		for (i = 0; i < CMC_HISTORY_LENGTH; i++) {
			if (now - cmc_history[i] <= HZ)
				count++;
		}

		IA64_MCA_DEBUG(KERN_INFO "CMC threshold %d/%d\n", count, CMC_HISTORY_LENGTH);
		if (count >= CMC_HISTORY_LENGTH) {

			cmc_polling_enabled = 1;
			spin_unlock(&cmc_history_lock);

			/*
			 * We rely on the local_irq_enable() above so
			 * that this can't deadlock.
			 */
			ia64_mca_cmc_vector_disable(NULL);

			smp_call_function(ia64_mca_cmc_vector_disable, NULL, 1, 0);

			/*
			 * Corrected errors will still be corrected, but
			 * make sure there's a log somewhere that indicates
			 * something is generating more than we can handle.
			 */
			printk(KERN_WARNING "%s: WARNING: Switching to polling CMC handler, error records may be lost\n", __FUNCTION__);

			mod_timer(&cmc_poll_timer, jiffies + CMC_POLL_INTERVAL);

			/* lock already released, get out now */
			return IRQ_HANDLED;
		} else {
			cmc_history[index++] = now;
			if (index == CMC_HISTORY_LENGTH)
				index = 0;
		}
	}
	spin_unlock(&cmc_history_lock);
	return IRQ_HANDLED;
}

/*
 * IA64_MCA log support
 */
#define IA64_MAX_LOGS		2	/* Double-buffering for nested MCAs */
#define IA64_MAX_LOG_TYPES      4   /* MCA, INIT, CMC, CPE */

typedef struct ia64_state_log_s
{
	spinlock_t	isl_lock;
	int		isl_index;
	unsigned long	isl_count;
	ia64_err_rec_t  *isl_log[IA64_MAX_LOGS]; /* need space to store header + error log */
} ia64_state_log_t;

static ia64_state_log_t ia64_state_log[IA64_MAX_LOG_TYPES];

#define IA64_LOG_ALLOCATE(it, size) \
	{ia64_state_log[it].isl_log[IA64_LOG_CURR_INDEX(it)] = \
		(ia64_err_rec_t *)alloc_bootmem(size); \
	ia64_state_log[it].isl_log[IA64_LOG_NEXT_INDEX(it)] = \
		(ia64_err_rec_t *)alloc_bootmem(size);}
#define IA64_LOG_LOCK_INIT(it) spin_lock_init(&ia64_state_log[it].isl_lock)
#define IA64_LOG_LOCK(it)      spin_lock_irqsave(&ia64_state_log[it].isl_lock, s)
#define IA64_LOG_UNLOCK(it)    spin_unlock_irqrestore(&ia64_state_log[it].isl_lock,s)
#define IA64_LOG_NEXT_INDEX(it)    ia64_state_log[it].isl_index
#define IA64_LOG_CURR_INDEX(it)    1 - ia64_state_log[it].isl_index
#define IA64_LOG_INDEX_INC(it) \
    {ia64_state_log[it].isl_index = 1 - ia64_state_log[it].isl_index; \
    ia64_state_log[it].isl_count++;}
#define IA64_LOG_INDEX_DEC(it) \
    ia64_state_log[it].isl_index = 1 - ia64_state_log[it].isl_index
#define IA64_LOG_NEXT_BUFFER(it)   (void *)((ia64_state_log[it].isl_log[IA64_LOG_NEXT_INDEX(it)]))
#define IA64_LOG_CURR_BUFFER(it)   (void *)((ia64_state_log[it].isl_log[IA64_LOG_CURR_INDEX(it)]))
#define IA64_LOG_COUNT(it)         ia64_state_log[it].isl_count

/*
 *  ia64_mca_cmc_int_caller
 *
 * 	Triggered by sw interrupt from CMC polling routine.  Calls
 * 	real interrupt handler and either triggers a sw interrupt
 * 	on the next cpu or does cleanup at the end.
 *
 * Inputs
 *	interrupt number
 *	client data arg ptr
 *	saved registers ptr
 * Outputs
 * 	handled
 */
irqreturn_t
ia64_mca_cmc_int_caller(int cpe_irq, void *arg, struct pt_regs *ptregs)
{
	static int start_count = -1;
	unsigned int cpuid;

	cpuid = smp_processor_id();

	/* If first cpu, update count */
	if (start_count == -1)
		start_count = IA64_LOG_COUNT(SAL_INFO_TYPE_CMC);

	ia64_mca_cmc_int_handler(cpe_irq, arg, ptregs);

	for (++cpuid ; cpuid < NR_CPUS && !cpu_online(cpuid) ; cpuid++);

	if (cpuid < NR_CPUS) {
		platform_send_ipi(cpuid, IA64_CMCP_VECTOR, IA64_IPI_DM_INT, 0);
	} else {
		/* If no log recored, switch out of polling mode */
		if (start_count == IA64_LOG_COUNT(SAL_INFO_TYPE_CMC)) {

			printk(KERN_WARNING "%s: Returning to interrupt driven CMC handler\n", __FUNCTION__);

			/*
			 * The cmc interrupt handler enabled irqs, so
			 * this can't deadlock.
			 */
			smp_call_function(ia64_mca_cmc_vector_enable, NULL, 1, 0);

			/*
			 * Turn off interrupts before re-enabling the
			 * cmc vector locally.  Make sure we get out.
			 */
			local_irq_disable();
			ia64_mca_cmc_vector_enable(NULL);
			cmc_polling_enabled = 0;

		} else {

			mod_timer(&cmc_poll_timer, jiffies + CMC_POLL_INTERVAL);
		}

		start_count = -1;
	}

	return IRQ_HANDLED;
}

/*
 *  ia64_mca_cmc_poll
 *
 *	Poll for Corrected Machine Checks (CMCs)
 *
 * Inputs   :   dummy(unused)
 * Outputs  :   None
 *
 */
static void
ia64_mca_cmc_poll (unsigned long dummy)
{
	/* Trigger a CMC interrupt cascade  */
	platform_send_ipi(first_cpu(cpu_online_map), IA64_CMCP_VECTOR, IA64_IPI_DM_INT, 0);
}

/*
 *  ia64_mca_cpe_int_caller
 *
 * 	Triggered by sw interrupt from CPE polling routine.  Calls
 * 	real interrupt handler and either triggers a sw interrupt
 * 	on the next cpu or does cleanup at the end.
 *
 * Inputs
 *	interrupt number
 *	client data arg ptr
 *	saved registers ptr
 * Outputs
 * 	handled
 */
irqreturn_t
ia64_mca_cpe_int_caller(int cpe_irq, void *arg, struct pt_regs *ptregs)
{
	static int start_count = -1;
	static int poll_time = MAX_CPE_POLL_INTERVAL;
	unsigned int cpuid;

	cpuid = smp_processor_id();

	/* If first cpu, update count */
	if (start_count == -1)
		start_count = IA64_LOG_COUNT(SAL_INFO_TYPE_CPE);

	ia64_mca_cpe_int_handler(cpe_irq, arg, ptregs);

	for (++cpuid ; cpuid < NR_CPUS && !cpu_online(cpuid) ; cpuid++);

	if (cpuid < NR_CPUS) {
		platform_send_ipi(cpuid, IA64_CPEP_VECTOR, IA64_IPI_DM_INT, 0);
	} else {
		/*
		 * If a log was recorded, increase our polling frequency,
		 * otherwise, backoff.
		 */
		if (start_count != IA64_LOG_COUNT(SAL_INFO_TYPE_CPE)) {
			poll_time = max(MIN_CPE_POLL_INTERVAL, poll_time / 2);
		} else {
			poll_time = min(MAX_CPE_POLL_INTERVAL, poll_time * 2);
		}
		start_count = -1;
		mod_timer(&cpe_poll_timer, jiffies + poll_time);
	}

	return IRQ_HANDLED;
}

/*
 *  ia64_mca_cpe_poll
 *
 *	Poll for Corrected Platform Errors (CPEs), trigger interrupt
 *	on first cpu, from there it will trickle through all the cpus.
 *
 * Inputs   :   dummy(unused)
 * Outputs  :   None
 *
 */
static void
ia64_mca_cpe_poll (unsigned long dummy)
{
	/* Trigger a CPE interrupt cascade  */
	platform_send_ipi(first_cpu(cpu_online_map), IA64_CPEP_VECTOR, IA64_IPI_DM_INT, 0);
}

/*
 * ia64_mca_late_init
 *
 *	Opportunity to setup things that require initialization later
 *	than ia64_mca_init.  Setup a timer to poll for CPEs if the
 *	platform doesn't support an interrupt driven mechanism.
 *
 *  Inputs  :   None
 *  Outputs :   Status
 */
static int __init
ia64_mca_late_init(void)
{
	init_timer(&cmc_poll_timer);
	cmc_poll_timer.function = ia64_mca_cmc_poll;

	/* Reset to the correct state */
	cmc_polling_enabled = 0;

	init_timer(&cpe_poll_timer);
	cpe_poll_timer.function = ia64_mca_cpe_poll;

#ifdef CONFIG_ACPI
	/* If platform doesn't support CPEI, get the timer going. */
	if (acpi_request_vector(ACPI_INTERRUPT_CPEI) < 0 && cpe_poll_enabled) {
		register_percpu_irq(IA64_CPEP_VECTOR, &mca_cpep_irqaction);
		ia64_mca_cpe_poll(0UL);
	}
#endif

	return 0;
}

device_initcall(ia64_mca_late_init);

/*
 * C portion of the OS INIT handler
 *
 * Called from ia64_monarch_init_handler
 *
 * Inputs: pointer to pt_regs where processor info was saved.
 *
 * Returns:
 *   0 if SAL must warm boot the System
 *   1 if SAL must return to interrupted context using PAL_MC_RESUME
 *
 */
void
ia64_init_handler (struct pt_regs *pt, struct switch_stack *sw)
{
	pal_min_state_area_t *ms;

	printk(KERN_INFO "Entered OS INIT handler. PSP=%lx\n",
		ia64_sal_to_os_handoff_state.proc_state_param);

	/*
	 * Address of minstate area provided by PAL is physical,
	 * uncacheable (bit 63 set). Convert to Linux virtual
	 * address in region 6.
	 */
	ms = (pal_min_state_area_t *)(ia64_sal_to_os_handoff_state.pal_min_state | (6ul<<61));

	init_handler_platform(ms, pt, sw);	/* call platform specific routines */
}

/*
 *  ia64_log_prt_guid
 *
 *  Print a formatted GUID.
 *
 * Inputs   :   p_guid      (ptr to the GUID)
 *              prfunc      (print function)
 * Outputs  :   None
 *
 */
void
ia64_log_prt_guid (efi_guid_t *p_guid, prfunc_t prfunc)
{
	char out[40];
	printk(KERN_DEBUG "GUID = %s\n", efi_guid_unparse(p_guid, out));
}

static void
ia64_log_hexdump(unsigned char *p, unsigned long n_ch, prfunc_t prfunc)
{
	unsigned long i;
	int j;

	if (!p)
		return;

	for (i = 0; i < n_ch;) {
		prfunc("%p ", (void *)p);
		for (j = 0; (j < 16) && (i < n_ch); i++, j++, p++) {
			prfunc("%02x ", *p);
		}
		prfunc("\n");
	}
}

#ifdef MCA_PRT_XTRA_DATA    // for test only @FVL

static void
ia64_log_prt_record_header (sal_log_record_header_t *rh, prfunc_t prfunc)
{
	prfunc("SAL RECORD HEADER:  Record buffer = %p,  header size = %ld\n",
	       (void *)rh, sizeof(sal_log_record_header_t));
	ia64_log_hexdump((unsigned char *)rh, sizeof(sal_log_record_header_t),
			 (prfunc_t)prfunc);
	prfunc("Total record length = %d\n", rh->len);
	ia64_log_prt_guid(&rh->platform_guid, prfunc);
	prfunc("End of SAL RECORD HEADER\n");
}

static void
ia64_log_prt_section_header (sal_log_section_hdr_t *sh, prfunc_t prfunc)
{
	prfunc("SAL SECTION HEADER:  Record buffer = %p,  header size = %ld\n",
	       (void *)sh, sizeof(sal_log_section_hdr_t));
	ia64_log_hexdump((unsigned char *)sh, sizeof(sal_log_section_hdr_t),
			 (prfunc_t)prfunc);
	prfunc("Length of section & header = %d\n", sh->len);
	ia64_log_prt_guid(&sh->guid, prfunc);
	prfunc("End of SAL SECTION HEADER\n");
}
#endif  // MCA_PRT_XTRA_DATA for test only @FVL

/*
 * ia64_log_init
 *	Reset the OS ia64 log buffer
 * Inputs   :   info_type   (SAL_INFO_TYPE_{MCA,INIT,CMC,CPE})
 * Outputs	:	None
 */
void
ia64_log_init(int sal_info_type)
{
	u64	max_size = 0;

	IA64_LOG_NEXT_INDEX(sal_info_type) = 0;
	IA64_LOG_LOCK_INIT(sal_info_type);

	// SAL will tell us the maximum size of any error record of this type
	max_size = ia64_sal_get_state_info_size(sal_info_type);
	if (!max_size)
		/* alloc_bootmem() doesn't like zero-sized allocations! */
		return;

	// set up OS data structures to hold error info
	IA64_LOG_ALLOCATE(sal_info_type, max_size);
	memset(IA64_LOG_CURR_BUFFER(sal_info_type), 0, max_size);
	memset(IA64_LOG_NEXT_BUFFER(sal_info_type), 0, max_size);
}

/*
 * ia64_log_get
 *
 *	Get the current MCA log from SAL and copy it into the OS log buffer.
 *
 *  Inputs  :   info_type   (SAL_INFO_TYPE_{MCA,INIT,CMC,CPE})
 *              prfunc      (fn ptr of log output function)
 *  Outputs :   size        (total record length)
 *
 */
u64
ia64_log_get(int sal_info_type, prfunc_t prfunc)
{
	sal_log_record_header_t     *log_buffer;
	u64                         total_len = 0;
	int                         s;

	IA64_LOG_LOCK(sal_info_type);

	/* Get the process state information */
	log_buffer = IA64_LOG_NEXT_BUFFER(sal_info_type);

	total_len = ia64_sal_get_state_info(sal_info_type, (u64 *)log_buffer);

	if (total_len) {
		IA64_LOG_INDEX_INC(sal_info_type);
		IA64_LOG_UNLOCK(sal_info_type);
		IA64_MCA_DEBUG("ia64_log_get: SAL error record type %d retrieved. "
			       "Record length = %ld\n", sal_info_type, total_len);
		return total_len;
	} else {
		IA64_LOG_UNLOCK(sal_info_type);
		return 0;
	}
}

/*
 *  ia64_log_prt_oem_data
 *
 *  Print OEM specific data if included.
 *
 * Inputs   :   header_len  (length passed in section header)
 *              sect_len    (default length of section type)
 *              p_data      (ptr to data)
 *			prfunc		(print function)
 * Outputs	:	None
 *
 */
void
ia64_log_prt_oem_data (int header_len, int sect_len, u8 *p_data, prfunc_t prfunc)
{
	int oem_data_len, i;

	if ((oem_data_len = header_len - sect_len) > 0) {
		prfunc(" OEM Specific Data:");
		for (i = 0; i < oem_data_len; i++, p_data++)
			prfunc(" %02x", *p_data);
	}
	prfunc("\n");
}

/*
 *  ia64_log_rec_header_print
 *
 *  Log info from the SAL error record header.
 *
 *  Inputs  :   lh *    (ptr to SAL log error record header)
 *              prfunc  (fn ptr of log output function to use)
 *  Outputs :   None
 */
void
ia64_log_rec_header_print (sal_log_record_header_t *lh, prfunc_t prfunc)
{
	prfunc("+Err Record ID: %d    SAL Rev: %2x.%02x\n", lh->id,
			lh->revision.major, lh->revision.minor);
	prfunc("+Time: %02x/%02x/%02x%02x %02x:%02x:%02x    Severity %d\n",
			lh->timestamp.slh_month, lh->timestamp.slh_day,
			lh->timestamp.slh_century, lh->timestamp.slh_year,
			lh->timestamp.slh_hour, lh->timestamp.slh_minute,
			lh->timestamp.slh_second, lh->severity);
}

/*
 * ia64_log_processor_regs_print
 *	Print the contents of the saved processor register(s) in the format
 *		<reg_prefix>[<index>] <value>
 *
 * Inputs	:	regs		(Register save buffer)
 *			reg_num	(# of registers)
 *			reg_class	(application/banked/control/bank1_general)
 *			reg_prefix	(ar/br/cr/b1_gr)
 * Outputs	:	None
 *
 */
void
ia64_log_processor_regs_print(u64	*regs,
			      int	reg_num,
			      char	*reg_class,
			      char	*reg_prefix,
			      prfunc_t	prfunc)
{
	int i;

	prfunc("+%s Registers\n", reg_class);
	for (i = 0; i < reg_num; i++)
		prfunc("+ %s[%d] 0x%lx\n", reg_prefix, i, regs[i]);
}

/*
 * ia64_log_processor_fp_regs_print
 *  Print the contents of the saved floating page register(s) in the format
 *      <reg_prefix>[<index>] <value>
 *
 * Inputs:  ia64_fpreg  (Register save buffer)
 *          reg_num     (# of registers)
 *          reg_class   (application/banked/control/bank1_general)
 *          reg_prefix  (ar/br/cr/b1_gr)
 * Outputs: None
 *
 */
void
ia64_log_processor_fp_regs_print (struct ia64_fpreg *regs,
                                  int               reg_num,
                                  char              *reg_class,
                                  char              *reg_prefix,
                                  prfunc_t          prfunc)
{
	int i;

	prfunc("+%s Registers\n", reg_class);
	for (i = 0; i < reg_num; i++)
		prfunc("+ %s[%d] 0x%lx%016lx\n", reg_prefix, i, regs[i].u.bits[1],
		       regs[i].u.bits[0]);
}

static char *pal_mesi_state[] = {
	"Invalid",
	"Shared",
	"Exclusive",
	"Modified",
	"Reserved1",
	"Reserved2",
	"Reserved3",
	"Reserved4"
};

static char *pal_cache_op[] = {
	"Unknown",
	"Move in",
	"Cast out",
	"Coherency check",
	"Internal",
	"Instruction fetch",
	"Implicit Writeback",
	"Reserved"
};

/*
 * ia64_log_cache_check_info_print
 *	Display the machine check information related to cache error(s).
 * Inputs:  i           (Multiple errors are logged, i - index of logged error)
 *          cc_info *   (Ptr to cache check info logged by the PAL and later
 *					 captured by the SAL)
 *          prfunc      (fn ptr of print function to be used for output)
 * Outputs: None
 */
void
ia64_log_cache_check_info_print (int                      i,
                                 sal_log_mod_error_info_t *cache_check_info,
				 prfunc_t		prfunc)
{
	pal_cache_check_info_t  *info;
	u64                     target_addr;

	if (!cache_check_info->valid.check_info) {
		IA64_MCA_DEBUG("ia64_mca_log_print: invalid cache_check_info[%d]\n",i);
		return;                 /* If check info data not valid, skip it */
	}

	info        = (pal_cache_check_info_t *)&cache_check_info->check_info;
	target_addr = cache_check_info->target_identifier;

	prfunc("+ Cache check info[%d]\n+", i);
	prfunc("  Level: L%d,",info->level);
	if (info->mv)
		prfunc(" Mesi: %s,",pal_mesi_state[info->mesi]);
	prfunc(" Index: %d,", info->index);
	if (info->ic)
		prfunc(" Cache: Instruction,");
	if (info->dc)
		prfunc(" Cache: Data,");
	if (info->tl)
		prfunc(" Line: Tag,");
	if (info->dl)
		prfunc(" Line: Data,");
	prfunc(" Operation: %s,", pal_cache_op[info->op]);
	if (info->wv)
		prfunc(" Way: %d,", info->way);
	if (cache_check_info->valid.target_identifier)
		/* Hope target address is saved in target_identifier */
		if (info->tv)
			prfunc(" Target Addr: 0x%lx,", target_addr);
	if (info->mc)
		prfunc(" MC: Corrected");
	prfunc("\n");
}

/*
 * ia64_log_tlb_check_info_print
 *	Display the machine check information related to tlb error(s).
 * Inputs:  i           (Multiple errors are logged, i - index of logged error)
 *          tlb_info *  (Ptr to machine check info logged by the PAL and later
 *					 captured by the SAL)
 *          prfunc      (fn ptr of print function to be used for output)
 * Outputs: None
 */
void
ia64_log_tlb_check_info_print (int                      i,
                               sal_log_mod_error_info_t *tlb_check_info,
                               prfunc_t                 prfunc)

{
	pal_tlb_check_info_t    *info;

	if (!tlb_check_info->valid.check_info) {
		IA64_MCA_DEBUG("ia64_mca_log_print: invalid tlb_check_info[%d]\n", i);
		return;                 /* If check info data not valid, skip it */
	}

	info = (pal_tlb_check_info_t *)&tlb_check_info->check_info;

	prfunc("+ TLB Check Info [%d]\n+", i);
	if (info->itc)
		prfunc("  Failure: Instruction Translation Cache");
	if (info->dtc)
		prfunc("  Failure: Data Translation Cache");
	if (info->itr) {
		prfunc("  Failure: Instruction Translation Register");
		prfunc(" ,Slot: %d", info->tr_slot);
	}
	if (info->dtr) {
		prfunc("  Failure: Data Translation Register");
		prfunc(" ,Slot: %d", info->tr_slot);
	}
	if (info->mc)
		prfunc(" ,MC: Corrected");
	prfunc("\n");
}

/*
 * ia64_log_bus_check_info_print
 *	Display the machine check information related to bus error(s).
 * Inputs:  i           (Multiple errors are logged, i - index of logged error)
 *          bus_info *  (Ptr to machine check info logged by the PAL and later
 *					 captured by the SAL)
 *          prfunc      (fn ptr of print function to be used for output)
 * Outputs: None
 */
void
ia64_log_bus_check_info_print (int                      i,
                               sal_log_mod_error_info_t *bus_check_info,
                               prfunc_t                 prfunc)
{
	pal_bus_check_info_t *info;
	u64         req_addr;   /* Address of the requestor of the transaction */
	u64         resp_addr;  /* Address of the responder of the transaction */
	u64         targ_addr;  /* Address where the data was to be delivered to */
	/* or obtained from */

	if (!bus_check_info->valid.check_info) {
		IA64_MCA_DEBUG("ia64_mca_log_print: invalid bus_check_info[%d]\n", i);
		return;                 /* If check info data not valid, skip it */
	}

	info      = (pal_bus_check_info_t *)&bus_check_info->check_info;
	req_addr  = bus_check_info->requestor_identifier;
	resp_addr = bus_check_info->responder_identifier;
	targ_addr = bus_check_info->target_identifier;

	prfunc("+ BUS Check Info [%d]\n+", i);
	prfunc(" Status Info: %d", info->bsi);
	prfunc(" ,Severity: %d", info->sev);
	prfunc(" ,Transaction Type: %d", info->type);
	prfunc(" ,Transaction Size: %d", info->size);
	if (info->cc)
		prfunc(" ,Cache-cache-transfer");
	if (info->ib)
		prfunc(" ,Error: Internal");
	if (info->eb)
		prfunc(" ,Error: External");
	if (info->mc)
		prfunc(" ,MC: Corrected");
	if (info->tv)
		prfunc(" ,Target Address: 0x%lx", targ_addr);
	if (info->rq)
		prfunc(" ,Requestor Address: 0x%lx", req_addr);
	if (info->tv)
		prfunc(" ,Responder Address: 0x%lx", resp_addr);
	prfunc("\n");
}

/*
 *  ia64_log_mem_dev_err_info_print
 *
 *  Format and log the platform memory device error record section data.
 *
 *  Inputs:  mem_dev_err_info * (Ptr to memory device error record section
 *                               returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_mem_dev_err_info_print (sal_log_mem_dev_err_info_t *mdei,
                                 prfunc_t                   prfunc)
{
	prfunc("+ Mem Error Detail: ");

	if (mdei->valid.error_status)
		prfunc(" Error Status: %#lx,", mdei->error_status);
	if (mdei->valid.physical_addr)
		prfunc(" Physical Address: %#lx,", mdei->physical_addr);
	if (mdei->valid.addr_mask)
		prfunc(" Address Mask: %#lx,", mdei->addr_mask);
	if (mdei->valid.node)
		prfunc(" Node: %d,", mdei->node);
	if (mdei->valid.card)
		prfunc(" Card: %d,", mdei->card);
	if (mdei->valid.module)
		prfunc(" Module: %d,", mdei->module);
	if (mdei->valid.bank)
		prfunc(" Bank: %d,", mdei->bank);
	if (mdei->valid.device)
		prfunc(" Device: %d,", mdei->device);
	if (mdei->valid.row)
		prfunc(" Row: %d,", mdei->row);
	if (mdei->valid.column)
		prfunc(" Column: %d,", mdei->column);
	if (mdei->valid.bit_position)
		prfunc(" Bit Position: %d,", mdei->bit_position);
	if (mdei->valid.target_id)
		prfunc(" ,Target Address: %#lx,", mdei->target_id);
	if (mdei->valid.requestor_id)
		prfunc(" ,Requestor Address: %#lx,", mdei->requestor_id);
	if (mdei->valid.responder_id)
		prfunc(" ,Responder Address: %#lx,", mdei->responder_id);
	if (mdei->valid.bus_spec_data)
		prfunc(" Bus Specific Data: %#lx,", mdei->bus_spec_data);
	prfunc("\n");

	if (mdei->valid.oem_id) {
		u8  *p_data = &(mdei->oem_id[0]);
		int i;

		prfunc(" OEM Memory Controller ID:");
		for (i = 0; i < 16; i++, p_data++)
			prfunc(" %02x", *p_data);
		prfunc("\n");
	}

	if (mdei->valid.oem_data) {
		platform_mem_dev_err_print((int)mdei->header.len,
				      (int)sizeof(sal_log_mem_dev_err_info_t) - 1,
				      &(mdei->oem_data[0]), prfunc);
	}
}

/*
 *  ia64_log_sel_dev_err_info_print
 *
 *  Format and log the platform SEL device error record section data.
 *
 *  Inputs:  sel_dev_err_info * (Ptr to the SEL device error record section
 *                               returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_sel_dev_err_info_print (sal_log_sel_dev_err_info_t *sdei,
                                 prfunc_t                   prfunc)
{
	int     i;

	prfunc("+ SEL Device Error Detail: ");

	if (sdei->valid.record_id)
		prfunc(" Record ID: %#x", sdei->record_id);
	if (sdei->valid.record_type)
		prfunc(" Record Type: %#x", sdei->record_type);
	prfunc(" Time Stamp: ");
	for (i = 0; i < 4; i++)
		prfunc("%1d", sdei->timestamp[i]);
	if (sdei->valid.generator_id)
		prfunc(" Generator ID: %#x", sdei->generator_id);
	if (sdei->valid.evm_rev)
		prfunc(" Message Format Version: %#x", sdei->evm_rev);
	if (sdei->valid.sensor_type)
		prfunc(" Sensor Type: %#x", sdei->sensor_type);
	if (sdei->valid.sensor_num)
		prfunc(" Sensor Number: %#x", sdei->sensor_num);
	if (sdei->valid.event_dir)
		prfunc(" Event Direction Type: %#x", sdei->event_dir);
	if (sdei->valid.event_data1)
		prfunc(" Data1: %#x", sdei->event_data1);
	if (sdei->valid.event_data2)
		prfunc(" Data2: %#x", sdei->event_data2);
	if (sdei->valid.event_data3)
		prfunc(" Data3: %#x", sdei->event_data3);
	prfunc("\n");

}

/*
 *  ia64_log_pci_bus_err_info_print
 *
 *  Format and log the platform PCI bus error record section data.
 *
 *  Inputs:  pci_bus_err_info * (Ptr to the PCI bus error record section
 *                               returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_pci_bus_err_info_print (sal_log_pci_bus_err_info_t *pbei,
                                 prfunc_t                   prfunc)
{
	prfunc("+ PCI Bus Error Detail: ");

	if (pbei->valid.err_status)
		prfunc(" Error Status: %#lx", pbei->err_status);
	if (pbei->valid.err_type)
		prfunc(" Error Type: %#x", pbei->err_type);
	if (pbei->valid.bus_id)
		prfunc(" Bus ID: %#x", pbei->bus_id);
	if (pbei->valid.bus_address)
		prfunc(" Bus Address: %#lx", pbei->bus_address);
	if (pbei->valid.bus_data)
		prfunc(" Bus Data: %#lx", pbei->bus_data);
	if (pbei->valid.bus_cmd)
		prfunc(" Bus Command: %#lx", pbei->bus_cmd);
	if (pbei->valid.requestor_id)
		prfunc(" Requestor ID: %#lx", pbei->requestor_id);
	if (pbei->valid.responder_id)
		prfunc(" Responder ID: %#lx", pbei->responder_id);
	if (pbei->valid.target_id)
		prfunc(" Target ID: %#lx", pbei->target_id);
	if (pbei->valid.oem_data)
		prfunc("\n");

	if (pbei->valid.oem_data) {
		platform_pci_bus_err_print((int)pbei->header.len,
				      (int)sizeof(sal_log_pci_bus_err_info_t) - 1,
				      &(pbei->oem_data[0]), prfunc);
	}
}

/*
 *  ia64_log_smbios_dev_err_info_print
 *
 *  Format and log the platform SMBIOS device error record section data.
 *
 *  Inputs:  smbios_dev_err_info * (Ptr to the SMBIOS device error record
 *                                  section returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_smbios_dev_err_info_print (sal_log_smbios_dev_err_info_t *sdei,
                                    prfunc_t                      prfunc)
{
	u8      i;

	prfunc("+ SMBIOS Device Error Detail: ");

	if (sdei->valid.event_type)
		prfunc(" Event Type: %#x", sdei->event_type);
	if (sdei->valid.time_stamp) {
		prfunc(" Time Stamp: ");
		for (i = 0; i < 6; i++)
			prfunc("%d", sdei->time_stamp[i]);
	}
	if ((sdei->valid.data) && (sdei->valid.length)) {
		prfunc(" Data: ");
		for (i = 0; i < sdei->length; i++)
			prfunc(" %02x", sdei->data[i]);
	}
	prfunc("\n");
}

/*
 *  ia64_log_pci_comp_err_info_print
 *
 *  Format and log the platform PCI component error record section data.
 *
 *  Inputs:  pci_comp_err_info * (Ptr to the PCI component error record section
 *                                returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_pci_comp_err_info_print(sal_log_pci_comp_err_info_t *pcei,
				 prfunc_t                     prfunc)
{
	u32     n_mem_regs, n_io_regs;
	u64     i, n_pci_data;
	u64     *p_reg_data;
	u8      *p_oem_data;

	prfunc("+ PCI Component Error Detail: ");

	if (pcei->valid.err_status)
		prfunc(" Error Status: %#lx\n", pcei->err_status);
	if (pcei->valid.comp_info)
		prfunc(" Component Info: Vendor Id = %#x, Device Id = %#x,"
		       " Class Code = %#x, Seg/Bus/Dev/Func = %d/%d/%d/%d\n",
		       pcei->comp_info.vendor_id, pcei->comp_info.device_id,
		       pcei->comp_info.class_code, pcei->comp_info.seg_num,
		       pcei->comp_info.bus_num, pcei->comp_info.dev_num,
		       pcei->comp_info.func_num);

	n_mem_regs = (pcei->valid.num_mem_regs) ? pcei->num_mem_regs : 0;
	n_io_regs =  (pcei->valid.num_io_regs)  ? pcei->num_io_regs  : 0;
	p_reg_data = &(pcei->reg_data_pairs[0]);
	p_oem_data = (u8 *)p_reg_data +
		(n_mem_regs + n_io_regs) * 2 * sizeof(u64);
	n_pci_data = p_oem_data - (u8 *)pcei;

	if (n_pci_data > pcei->header.len) {
		prfunc(" Invalid PCI Component Error Record format: length = %ld, "
		       " Size PCI Data = %d, Num Mem-Map/IO-Map Regs = %ld/%ld\n",
		       pcei->header.len, n_pci_data, n_mem_regs, n_io_regs);
		return;
	}

	if (n_mem_regs) {
		prfunc(" Memory Mapped Registers\n Address \tValue\n");
		for (i = 0; i < pcei->num_mem_regs; i++) {
			prfunc(" %#lx %#lx\n", p_reg_data[0], p_reg_data[1]);
			p_reg_data += 2;
		}
	}
	if (n_io_regs) {
		prfunc(" I/O Mapped Registers\n Address \tValue\n");
		for (i = 0; i < pcei->num_io_regs; i++) {
			prfunc(" %#lx %#lx\n", p_reg_data[0], p_reg_data[1]);
			p_reg_data += 2;
		}
	}
	if (pcei->valid.oem_data) {
		platform_pci_comp_err_print((int)pcei->header.len, n_pci_data,
				      p_oem_data, prfunc);
		prfunc("\n");
	}
}

/*
 *  ia64_log_plat_specific_err_info_print
 *
 *  Format and log the platform specifie error record section data.
 *
 *  Inputs:  sel_dev_err_info * (Ptr to the platform specific error record
 *                               section returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_plat_specific_err_info_print (sal_log_plat_specific_err_info_t *psei,
                                       prfunc_t                         prfunc)
{
	prfunc("+ Platform Specific Error Detail: ");

	if (psei->valid.err_status)
		prfunc(" Error Status: %#lx", psei->err_status);
	if (psei->valid.guid) {
		prfunc(" GUID: ");
		ia64_log_prt_guid(&psei->guid, prfunc);
	}
	if (psei->valid.oem_data) {
		platform_plat_specific_err_print((int)psei->header.len,
				      (int)sizeof(sal_log_plat_specific_err_info_t) - 1,
				      &(psei->oem_data[0]), prfunc);
	}
	prfunc("\n");
}

/*
 *  ia64_log_host_ctlr_err_info_print
 *
 *  Format and log the platform host controller error record section data.
 *
 *  Inputs:  host_ctlr_err_info * (Ptr to the host controller error record
 *                                 section returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_host_ctlr_err_info_print (sal_log_host_ctlr_err_info_t *hcei,
                                   prfunc_t                     prfunc)
{
	prfunc("+ Host Controller Error Detail: ");

	if (hcei->valid.err_status)
		prfunc(" Error Status: %#lx", hcei->err_status);
	if (hcei->valid.requestor_id)
		prfunc(" Requestor ID: %#lx", hcei->requestor_id);
	if (hcei->valid.responder_id)
		prfunc(" Responder ID: %#lx", hcei->responder_id);
	if (hcei->valid.target_id)
		prfunc(" Target ID: %#lx", hcei->target_id);
	if (hcei->valid.bus_spec_data)
		prfunc(" Bus Specific Data: %#lx", hcei->bus_spec_data);
	if (hcei->valid.oem_data) {
		platform_host_ctlr_err_print((int)hcei->header.len,
				      (int)sizeof(sal_log_host_ctlr_err_info_t) - 1,
				      &(hcei->oem_data[0]), prfunc);
	}
	prfunc("\n");
}

/*
 *  ia64_log_plat_bus_err_info_print
 *
 *  Format and log the platform bus error record section data.
 *
 *  Inputs:  plat_bus_err_info * (Ptr to the platform bus error record section
 *                                returned by SAL)
 *           prfunc             (fn ptr of print function to be used for output)
 *  Outputs: None
 */
void
ia64_log_plat_bus_err_info_print (sal_log_plat_bus_err_info_t *pbei,
                                  prfunc_t                    prfunc)
{
	prfunc("+ Platform Bus Error Detail: ");

	if (pbei->valid.err_status)
		prfunc(" Error Status: %#lx", pbei->err_status);
	if (pbei->valid.requestor_id)
		prfunc(" Requestor ID: %#lx", pbei->requestor_id);
	if (pbei->valid.responder_id)
		prfunc(" Responder ID: %#lx", pbei->responder_id);
	if (pbei->valid.target_id)
		prfunc(" Target ID: %#lx", pbei->target_id);
	if (pbei->valid.bus_spec_data)
		prfunc(" Bus Specific Data: %#lx", pbei->bus_spec_data);
	if (pbei->valid.oem_data) {
		platform_plat_bus_err_print((int)pbei->header.len,
				      (int)sizeof(sal_log_plat_bus_err_info_t) - 1,
				      &(pbei->oem_data[0]), prfunc);
	}
	prfunc("\n");
}

/*
 *  ia64_log_proc_dev_err_info_print
 *
 *  Display the processor device error record.
 *
 *  Inputs:  sal_log_processor_info_t * (Ptr to processor device error record
 *                                       section body).
 *           prfunc                     (fn ptr of print function to be used
 *                                       for output).
 *  Outputs: None
 */
void
ia64_log_proc_dev_err_info_print (sal_log_processor_info_t  *slpi,
                                  prfunc_t                  prfunc)
{
#ifdef MCA_PRT_XTRA_DATA
	size_t  d_len = slpi->header.len - sizeof(sal_log_section_hdr_t);
#endif
	sal_processor_static_info_t *spsi;
	int                         i;
	sal_log_mod_error_info_t    *p_data;

	prfunc("+Processor Device Error Info Section\n");

#ifdef MCA_PRT_XTRA_DATA    // for test only @FVL
	{
		char    *p_data = (char *)&slpi->valid;

		prfunc("SAL_PROC_DEV_ERR SECTION DATA:  Data buffer = %p, "
		       "Data size = %ld\n", (void *)p_data, d_len);
		ia64_log_hexdump(p_data, d_len, prfunc);
		prfunc("End of SAL_PROC_DEV_ERR SECTION DATA\n");
	}
#endif  // MCA_PRT_XTRA_DATA for test only @FVL

	if (slpi->valid.proc_error_map)
		prfunc(" Processor Error Map: %#lx\n", slpi->proc_error_map);

	if (slpi->valid.proc_state_param)
		prfunc(" Processor State Param: %#lx\n", slpi->proc_state_parameter);

	if (slpi->valid.proc_cr_lid)
		prfunc(" Processor LID: %#lx\n", slpi->proc_cr_lid);

	/*
	 *  Note: March 2001 SAL spec states that if the number of elements in any
	 *  of  the MOD_ERROR_INFO_STRUCT arrays is zero, the entire array is
	 *  absent. Also, current implementations only allocate space for number of
	 *  elements used.  So we walk the data pointer from here on.
	 */
	p_data = &slpi->info[0];

	/* Print the cache check information if any*/
	for (i = 0 ; i < slpi->valid.num_cache_check; i++, p_data++)
		ia64_log_cache_check_info_print(i, p_data, prfunc);

	/* Print the tlb check information if any*/
	for (i = 0 ; i < slpi->valid.num_tlb_check; i++, p_data++)
		ia64_log_tlb_check_info_print(i, p_data, prfunc);

	/* Print the bus check information if any*/
	for (i = 0 ; i < slpi->valid.num_bus_check; i++, p_data++)
		ia64_log_bus_check_info_print(i, p_data, prfunc);

	/* Print the reg file check information if any*/
	for (i = 0 ; i < slpi->valid.num_reg_file_check; i++, p_data++)
		ia64_log_hexdump((u8 *)p_data, sizeof(sal_log_mod_error_info_t),
				 prfunc);    /* Just hex dump for now */

	/* Print the ms check information if any*/
	for (i = 0 ; i < slpi->valid.num_ms_check; i++, p_data++)
		ia64_log_hexdump((u8 *)p_data, sizeof(sal_log_mod_error_info_t),
				 prfunc);    /* Just hex dump for now */

	/* Print CPUID registers if any*/
	if (slpi->valid.cpuid_info) {
		u64     *p = (u64 *)p_data;

		prfunc(" CPUID Regs: %#lx %#lx %#lx %#lx\n", p[0], p[1], p[2], p[3]);
		p_data++;
	}

	/* Print processor static info if any */
	if (slpi->valid.psi_static_struct) {
		spsi = (sal_processor_static_info_t *)p_data;

		/* Print branch register contents if valid */
		if (spsi->valid.br)
			ia64_log_processor_regs_print(spsi->br, 8, "Branch", "br",
						      prfunc);

		/* Print control register contents if valid */
		if (spsi->valid.cr)
			ia64_log_processor_regs_print(spsi->cr, 128, "Control", "cr",
						      prfunc);

		/* Print application register contents if valid */
		if (spsi->valid.ar)
			ia64_log_processor_regs_print(spsi->ar, 128, "Application",
						      "ar", prfunc);

		/* Print region register contents if valid */
		if (spsi->valid.rr)
			ia64_log_processor_regs_print(spsi->rr, 8, "Region", "rr",
						      prfunc);

		/* Print floating-point register contents if valid */
		if (spsi->valid.fr)
			ia64_log_processor_fp_regs_print(spsi->fr, 128, "Floating-point", "fr",
							 prfunc);
	}
}

/*
 * ia64_log_processor_info_print
 *
 *	Display the processor-specific information logged by PAL as a part
 *	of MCA or INIT or CMC.
 *
 *  Inputs   :  lh      (Pointer of the sal log header which specifies the
 *                       format of SAL state info as specified by the SAL spec).
 *              prfunc  (fn ptr of print function to be used for output).
 * Outputs	:	None
 */
void
ia64_log_processor_info_print(sal_log_record_header_t *lh, prfunc_t prfunc)
{
	sal_log_section_hdr_t       *slsh;
	int                         n_sects;
	u32                         ercd_pos;

	if (!lh)
		return;

#ifdef MCA_PRT_XTRA_DATA    // for test only @FVL
	ia64_log_prt_record_header(lh, prfunc);
#endif  // MCA_PRT_XTRA_DATA for test only @FVL

	if ((ercd_pos = sizeof(sal_log_record_header_t)) >= lh->len) {
		IA64_MCA_DEBUG("ia64_mca_log_print: "
			       "truncated SAL CMC error record. len = %d\n",
			       lh->len);
		return;
	}

	/* Print record header info */
	ia64_log_rec_header_print(lh, prfunc);

	for (n_sects = 0; (ercd_pos < lh->len); n_sects++, ercd_pos += slsh->len) {
		/* point to next section header */
		slsh = (sal_log_section_hdr_t *)((char *)lh + ercd_pos);

#ifdef MCA_PRT_XTRA_DATA    // for test only @FVL
		ia64_log_prt_section_header(slsh, prfunc);
#endif  // MCA_PRT_XTRA_DATA for test only @FVL

		if (verify_guid(&slsh->guid, &(SAL_PROC_DEV_ERR_SECT_GUID))) {
			IA64_MCA_DEBUG("ia64_mca_log_print: unsupported record section\n");
			continue;
		}

		/*
		 *  Now process processor device error record section
		 */
		ia64_log_proc_dev_err_info_print((sal_log_processor_info_t *)slsh, printk);
	}

	IA64_MCA_DEBUG("ia64_mca_log_print: "
		       "found %d sections in SAL CMC error record. len = %d\n",
		       n_sects, lh->len);
	if (!n_sects) {
		prfunc("No Processor Device Error Info Section found\n");
		return;
	}
}

/*
 *  ia64_log_platform_info_print
 *
 *  Format and Log the SAL Platform Error Record.
 *
 *  Inputs  :   lh      (Pointer to the sal error record header with format
 *                       specified by the SAL spec).
 *              prfunc  (fn ptr of log output function to use)
 *  Outputs :	platform error status
 */
int
ia64_log_platform_info_print (sal_log_record_header_t *lh, prfunc_t prfunc)
{
	sal_log_section_hdr_t	*slsh;
	int			n_sects;
	u32			ercd_pos;
	int			platform_err = 0;

	if (!lh)
		return platform_err;

#ifdef MCA_PRT_XTRA_DATA    // for test only @FVL
	ia64_log_prt_record_header(lh, prfunc);
#endif  // MCA_PRT_XTRA_DATA for test only @FVL

	if ((ercd_pos = sizeof(sal_log_record_header_t)) >= lh->len) {
		IA64_MCA_DEBUG("ia64_mca_log_print: "
			       "truncated SAL error record. len = %d\n",
			       lh->len);
		return platform_err;
	}

	/* Print record header info */
	ia64_log_rec_header_print(lh, prfunc);

	for (n_sects = 0; (ercd_pos < lh->len); n_sects++, ercd_pos += slsh->len) {
		/* point to next section header */
		slsh = (sal_log_section_hdr_t *)((char *)lh + ercd_pos);

#ifdef MCA_PRT_XTRA_DATA    // for test only @FVL
		ia64_log_prt_section_header(slsh, prfunc);

		if (efi_guidcmp(slsh->guid, SAL_PROC_DEV_ERR_SECT_GUID) != 0) {
			size_t  d_len = slsh->len - sizeof(sal_log_section_hdr_t);
			char    *p_data = (char *)&((sal_log_mem_dev_err_info_t *)slsh)->valid;

			prfunc("Start of Platform Err Data Section:  Data buffer = %p, "
			       "Data size = %ld\n", (void *)p_data, d_len);
			ia64_log_hexdump(p_data, d_len, prfunc);
			prfunc("End of Platform Err Data Section\n");
		}
#endif  // MCA_PRT_XTRA_DATA for test only @FVL

		/*
		 *  Now process CPE error record section
		 */
		if (efi_guidcmp(slsh->guid, SAL_PROC_DEV_ERR_SECT_GUID) == 0) {
			ia64_log_proc_dev_err_info_print((sal_log_processor_info_t *)slsh,
							 prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_MEM_DEV_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform Memory Device Error Info Section\n");
			ia64_log_mem_dev_err_info_print((sal_log_mem_dev_err_info_t *)slsh,
							prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_SEL_DEV_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform SEL Device Error Info Section\n");
			ia64_log_sel_dev_err_info_print((sal_log_sel_dev_err_info_t *)slsh,
							prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_PCI_BUS_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform PCI Bus Error Info Section\n");
			ia64_log_pci_bus_err_info_print((sal_log_pci_bus_err_info_t *)slsh,
							prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_SMBIOS_DEV_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform SMBIOS Device Error Info Section\n");
			ia64_log_smbios_dev_err_info_print((sal_log_smbios_dev_err_info_t *)slsh,
							   prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_PCI_COMP_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform PCI Component Error Info Section\n");
			ia64_log_pci_comp_err_info_print((sal_log_pci_comp_err_info_t *)slsh,
							 prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_SPECIFIC_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform Specific Error Info Section\n");
			ia64_log_plat_specific_err_info_print((sal_log_plat_specific_err_info_t *)
							      slsh,
							      prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_HOST_CTLR_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform Host Controller Error Info Section\n");
			ia64_log_host_ctlr_err_info_print((sal_log_host_ctlr_err_info_t *)slsh,
							  prfunc);
		} else if (efi_guidcmp(slsh->guid, SAL_PLAT_BUS_ERR_SECT_GUID) == 0) {
			platform_err = 1;
			prfunc("+Platform Bus Error Info Section\n");
			ia64_log_plat_bus_err_info_print((sal_log_plat_bus_err_info_t *)slsh,
							 prfunc);
		} else {
			IA64_MCA_DEBUG("ia64_mca_log_print: unsupported record section\n");
			continue;
		}
	}

	IA64_MCA_DEBUG("ia64_mca_log_print: found %d sections in SAL error record. len = %d\n",
		       n_sects, lh->len);
	if (!n_sects) {
		prfunc("No Platform Error Info Sections found\n");
		return platform_err;
	}
	return platform_err;
}

/*
 * ia64_log_print
 *
 *  Displays the contents of the OS error log information
 *
 *  Inputs   :  info_type   (SAL_INFO_TYPE_{MCA,INIT,CMC,CPE})
 *              prfunc      (fn ptr of log output function to use)
 * Outputs	:	platform error status
 */
int
ia64_log_print(int sal_info_type, prfunc_t prfunc)
{
	int platform_err = 0;

	switch(sal_info_type) {
	      case SAL_INFO_TYPE_MCA:
		prfunc("+BEGIN HARDWARE ERROR STATE AT MCA\n");
		platform_err = ia64_log_platform_info_print(IA64_LOG_CURR_BUFFER(sal_info_type),
							    prfunc);
		prfunc("+END HARDWARE ERROR STATE AT MCA\n");
		break;
	      case SAL_INFO_TYPE_INIT:
		prfunc("+MCA INIT ERROR LOG (UNIMPLEMENTED)\n");
		break;
	      case SAL_INFO_TYPE_CMC:
		prfunc("+BEGIN HARDWARE ERROR STATE AT CMC\n");
		ia64_log_processor_info_print(IA64_LOG_CURR_BUFFER(sal_info_type), prfunc);
		prfunc("+END HARDWARE ERROR STATE AT CMC\n");
		break;
	      case SAL_INFO_TYPE_CPE:
		prfunc("+BEGIN HARDWARE ERROR STATE AT CPE\n");
		ia64_log_platform_info_print(IA64_LOG_CURR_BUFFER(sal_info_type), prfunc);
		prfunc("+END HARDWARE ERROR STATE AT CPE\n");
		break;
	      default:
		prfunc("+MCA UNKNOWN ERROR LOG (UNIMPLEMENTED)\n");
		break;
	}
	return platform_err;
}

static int __init
ia64_mca_disable_cpe_polling(char *str)
{
	cpe_poll_enabled = 0;
	return 1;
}

__setup("disable_cpe_poll", ia64_mca_disable_cpe_polling);
