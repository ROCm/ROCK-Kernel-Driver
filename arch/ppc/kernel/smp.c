/*
 * Smp support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * Support for PReP (Motorola MTX/MVME) and Macintosh G4 SMP 
 * by Troy Benjegerdes (hozer@drgw.net)
 *
 * Support for DayStar quad CPU cards
 * Copyright (C) XLR8, Inc. 1994-2000
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/residual.h>
#include <asm/feature.h>
#include <asm/time.h>
#include <asm/gemini.h>

#include "open_pic.h"
int smp_threads_ready;
volatile int smp_commenced;
int smp_num_cpus = 1;
int smp_tb_synchronized;
struct cpuinfo_PPC cpu_data[NR_CPUS];
struct klock_info_struct klock_info = { KLOCK_CLEAR, 0 };
atomic_t ipi_recv;
atomic_t ipi_sent;
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;
unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];
cycles_t cacheflush_time;
static int max_cpus __initdata = NR_CPUS;
unsigned long cpu_online_map;
int smp_hw_index[NR_CPUS];

/* all cpu mappings are 1-1 -- Cort */
volatile unsigned long cpu_callin_map[NR_CPUS];

#define TB_SYNC_PASSES 4
volatile unsigned long __initdata tb_sync_flag = 0;
volatile unsigned long __initdata tb_offset = 0;

int start_secondary(void *);
extern int cpu_idle(void *unused);
void smp_call_function_interrupt(void);
void smp_message_pass(int target, int msg, unsigned long data, int wait);

extern void __secondary_start_psurge(void);
extern void __secondary_start_psurge2(void);	/* Temporary horrible hack */
extern void __secondary_start_psurge3(void);	/* Temporary horrible hack */

/* Addresses for powersurge registers */
#define HAMMERHEAD_BASE		0xf8000000
#define HHEAD_CONFIG		0x90
#define HHEAD_SEC_INTR		0xc0

/* register for interrupting the primary processor on the powersurge */
/* N.B. this is actually the ethernet ROM! */
#define PSURGE_PRI_INTR		0xf3019000

/* register for storing the start address for the secondary processor */
/* N.B. this is the PCI config space address register for the 1st bridge */
#define PSURGE_START		0xf2800000

/* Daystar/XLR8 4-CPU card */
#define PSURGE_QUAD_REG_ADDR	0xf8800000

#define PSURGE_QUAD_IRQ_SET	0
#define PSURGE_QUAD_IRQ_CLR	1
#define PSURGE_QUAD_IRQ_PRIMARY	2
#define PSURGE_QUAD_CKSTOP_CTL	3
#define PSURGE_QUAD_PRIMARY_ARB	4
#define PSURGE_QUAD_BOARD_ID	6
#define PSURGE_QUAD_WHICH_CPU	7
#define PSURGE_QUAD_CKSTOP_RDBK	8
#define PSURGE_QUAD_RESET_CTL	11

#define PSURGE_QUAD_OUT(r, v)	(out_8(quad_base + ((r) << 4) + 4, (v)))
#define PSURGE_QUAD_IN(r)	(in_8(quad_base + ((r) << 4) + 4) & 0x0f)
#define PSURGE_QUAD_BIS(r, v)	(PSURGE_QUAD_OUT((r), PSURGE_QUAD_IN(r) | (v)))
#define PSURGE_QUAD_BIC(r, v)	(PSURGE_QUAD_OUT((r), PSURGE_QUAD_IN(r) & ~(v)))

/* virtual addresses for the above */
static volatile u8 *hhead_base;
static volatile u8 *quad_base;
static volatile u32 *psurge_pri_intr;
static volatile u8 *psurge_sec_intr;
static volatile u32 *psurge_start;

/* what sort of powersurge board we have */
static int psurge_type;

/* values for psurge_type */
#define PSURGE_DUAL		0
#define PSURGE_QUAD_OKEE	1
#define PSURGE_QUAD_COTTON	2
#define PSURGE_QUAD_ICEGRASS	3

/* l2 cache stuff for dual G4 macs */
extern void core99_init_l2(void);

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 * 
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION	0
#define PPC_MSG_RESCHEDULE	1
#define PPC_MSG_INVALIDATE_TLB	2
#define PPC_MSG_XMON_BREAK	3

static inline void set_tb(unsigned int upper, unsigned int lower)
{
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, upper);
	mtspr(SPRN_TBWL, lower);
}

/*
 * Set and clear IPIs for powersurge.
 */
static inline void psurge_set_ipi(int cpu)
{
	if (cpu == 0)
		in_be32(psurge_pri_intr);
	else if (psurge_type == PSURGE_DUAL)
		out_8(psurge_sec_intr, 0);
	else
		PSURGE_QUAD_OUT(PSURGE_QUAD_IRQ_SET, 1 << cpu);
}

static inline void psurge_clr_ipi(int cpu)
{
	if (cpu > 0) {
		if (psurge_type == PSURGE_DUAL)
			out_8(psurge_sec_intr, ~0);
		else
			PSURGE_QUAD_OUT(PSURGE_QUAD_IRQ_CLR, 1 << cpu);
	}
}

/*
 * On powersurge (old SMP powermac architecture) we don't have
 * separate IPIs for separate messages like openpic does.  Instead
 * we have a bitmap for each processor, where a 1 bit means that
 * the corresponding message is pending for that processor.
 * Ideally each cpu's entry would be in a different cache line.
 *  -- paulus.
 */
static unsigned long psurge_smp_message[NR_CPUS];

void psurge_smp_message_recv(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int msg;

	/* clear interrupt */
	psurge_clr_ipi(cpu);

	if (smp_num_cpus < 2)
		return;

	/* make sure there is a message there */
	for (msg = 0; msg < 4; msg++)
		if (test_and_clear_bit(msg, &psurge_smp_message[cpu]))
			smp_message_recv(msg, regs);
}

void
psurge_primary_intr(int irq, void *d, struct pt_regs *regs)
{
	psurge_smp_message_recv(regs);
}

static void
smp_psurge_message_pass(int target, int msg, unsigned long data, int wait)
{
	int i;

	if (smp_num_cpus < 2)
		return;

	for (i = 0; i < smp_num_cpus; i++) {
		if (target == MSG_ALL
		    || (target == MSG_ALL_BUT_SELF && i != smp_processor_id())
		    || target == i) {
			set_bit(msg, &psurge_smp_message[i]);
			psurge_set_ipi(i);
		}
	}
}

/*
 * Determine a quad card presence. We read the board ID register, we
 * force the data bus to change to something else, and we read it again.
 * It it's stable, then the register probably exist (ugh !)
 */
static int __init psurge_quad_probe(void)
{
	int type;
	unsigned int i;

	type = PSURGE_QUAD_IN(PSURGE_QUAD_BOARD_ID);
	if (type < PSURGE_QUAD_OKEE || type > PSURGE_QUAD_ICEGRASS
	    || type != PSURGE_QUAD_IN(PSURGE_QUAD_BOARD_ID))
		return PSURGE_DUAL;

	/* looks OK, try a slightly more rigorous test */
	/* bogus is not necessarily cacheline-aligned,
	   though I don't suppose that really matters.  -- paulus */
	for (i = 0; i < 100; i++) {
		volatile u32 bogus[8];
		bogus[(0+i)%8] = 0x00000000;
		bogus[(1+i)%8] = 0x55555555;
		bogus[(2+i)%8] = 0xFFFFFFFF;
		bogus[(3+i)%8] = 0xAAAAAAAA;
		bogus[(4+i)%8] = 0x33333333;
		bogus[(5+i)%8] = 0xCCCCCCCC;
		bogus[(6+i)%8] = 0xCCCCCCCC;
		bogus[(7+i)%8] = 0x33333333;
		wmb();
		asm volatile("dcbf 0,%0" : : "r" (bogus) : "memory");
		mb();
		if (type != PSURGE_QUAD_IN(PSURGE_QUAD_BOARD_ID))
			return PSURGE_DUAL;
	}
	return type;
}

static void __init psurge_quad_init(void)
{
	int procbits;

	if (ppc_md.progress) ppc_md.progress("psurge_quad_init", 0x351);
	procbits = ~PSURGE_QUAD_IN(PSURGE_QUAD_WHICH_CPU);
	if (psurge_type == PSURGE_QUAD_ICEGRASS)
		PSURGE_QUAD_BIS(PSURGE_QUAD_RESET_CTL, procbits);
	else
		PSURGE_QUAD_BIC(PSURGE_QUAD_CKSTOP_CTL, procbits);
	mdelay(33);
	out_8(psurge_sec_intr, ~0);
	PSURGE_QUAD_OUT(PSURGE_QUAD_IRQ_CLR, procbits);
	PSURGE_QUAD_BIS(PSURGE_QUAD_RESET_CTL, procbits);
	if (psurge_type != PSURGE_QUAD_ICEGRASS)
		PSURGE_QUAD_BIS(PSURGE_QUAD_CKSTOP_CTL, procbits);
	PSURGE_QUAD_BIC(PSURGE_QUAD_PRIMARY_ARB, procbits);
	mdelay(33);
	PSURGE_QUAD_BIC(PSURGE_QUAD_RESET_CTL, procbits);
	mdelay(33);
	PSURGE_QUAD_BIS(PSURGE_QUAD_PRIMARY_ARB, procbits);
	mdelay(33);
}

static int __init smp_psurge_probe(void)
{
	int i, ncpus;

	/* We don't do SMP on the PPC601 -- paulus */
	if ((_get_PVR() >> 16) == 1)
		return 1;

	/*
	 * The powersurge cpu board can be used in the generation
	 * of powermacs that have a socket for an upgradeable cpu card,
	 * including the 7500, 8500, 9500, 9600.
	 * The device tree doesn't tell you if you have 2 cpus because
	 * OF doesn't know anything about the 2nd processor.
	 * Instead we look for magic bits in magic registers,
	 * in the hammerhead memory controller in the case of the
	 * dual-cpu powersurge board.  -- paulus.
	 */
	if (find_devices("hammerhead") == NULL)
		return 1;

	hhead_base = ioremap(HAMMERHEAD_BASE, 0x800);
	quad_base = ioremap(PSURGE_QUAD_REG_ADDR, 1024);
	psurge_sec_intr = hhead_base + HHEAD_SEC_INTR;

	psurge_type = psurge_quad_probe();
	if (psurge_type != PSURGE_DUAL) {
		psurge_quad_init();
		/* All released cards using this HW design have 4 CPUs */
		ncpus = 4;
	} else {
		iounmap((void *) quad_base);
		if ((in_8(hhead_base + HHEAD_CONFIG) & 0x02) == 0) {
			/* not a dual-cpu card */
			iounmap((void *) hhead_base);
			return 1;
		}
		ncpus = 2;
	}

	psurge_start = ioremap(PSURGE_START, 4);
	psurge_pri_intr = ioremap(PSURGE_PRI_INTR, 4);

	/* this is not actually strictly necessary -- paulus. */
	for (i = 1; i < ncpus; ++i)
		smp_hw_index[i] = i;

	if (ppc_md.progress) ppc_md.progress("smp_psurge_probe - done", 0x352);

	return ncpus;
}

static void __init smp_psurge_kick_cpu(int nr)
{
	void (*start)(void) = __secondary_start_psurge;

	if (ppc_md.progress) ppc_md.progress("smp_psurge_kick_cpu", 0x353);

	/* setup entry point of secondary processor */
	switch (nr) {
	case 2:
		start = __secondary_start_psurge2;
		break;
	case 3:
		start = __secondary_start_psurge3;
		break;
	}

	out_be32(psurge_start, __pa(start));
	mb();

	psurge_set_ipi(nr);
	udelay(10);
	psurge_clr_ipi(nr);

	if (ppc_md.progress) ppc_md.progress("smp_psurge_kick_cpu - done", 0x354);
}

/*
 * With the dual-cpu powersurge board, the decrementers and timebases
 * of both cpus are frozen after the secondary cpu is started up,
 * until we give the secondary cpu another interrupt.  This routine
 * uses this to get the timebases synchronized.
 *  -- paulus.
 */
static void __init psurge_dual_sync_tb(int cpu_nr)
{
	static volatile int sec_tb_reset = 0;
	int t;

	set_dec(tb_ticks_per_jiffy);
	set_tb(0, 0);
	last_jiffy_stamp(cpu_nr) = 0;

	if (cpu_nr > 0) {
		mb();
		sec_tb_reset = 1;
		return;
	}

	/* wait for the secondary to have reset its TB before proceeding */
	for (t = 10000000; t > 0 && !sec_tb_reset; --t)
		;

	/* now interrupt the secondary, starting both TBs */
	psurge_set_ipi(1);

	smp_tb_synchronized = 1;
}

static void
smp_psurge_setup_cpu(int cpu_nr)
{

	if (cpu_nr == 0) {
		if (smp_num_cpus < 2)
			return;
		/* reset the entry point so if we get another intr we won't
		 * try to startup again */
		out_be32(psurge_start, 0x100);
		if (request_irq(30, psurge_primary_intr, 0, "primary IPI", 0))
			printk(KERN_ERR "Couldn't get primary IPI interrupt");
	}

	if (psurge_type == PSURGE_DUAL)
		psurge_dual_sync_tb(cpu_nr);
}


static void
smp_openpic_message_pass(int target, int msg, unsigned long data, int wait)
{
	/* make sure we're sending something that translates to an IPI */
	if ( msg > 0x3 ){
		printk("SMP %d: smp_message_pass: unknown msg %d\n",
		       smp_processor_id(), msg);
		return;
	}
	switch ( target )
	{
	case MSG_ALL:
		openpic_cause_IPI(msg, 0xffffffff);
		break;
	case MSG_ALL_BUT_SELF:
		openpic_cause_IPI(msg,
				  0xffffffff & ~(1 << smp_processor_id()));
		break;
	default:
		openpic_cause_IPI(msg, 1<<target);
		break;
	}
}

static int
smp_core99_probe(void)
{
	struct device_node *cpus;
	int i, ncpus = 1;

	if (ppc_md.progress) ppc_md.progress("smp_core99_probe", 0x345);
	cpus = find_type_devices("cpu");
	if (cpus)
		while ((cpus = cpus->next) != NULL)
			++ncpus;
	printk("smp_core99_probe: found %d cpus\n", ncpus);
	if (ncpus > 1) {
		openpic_request_IPIs();
		for (i = 1; i < ncpus; ++i)
			smp_hw_index[i] = i;
	}

	return ncpus;
}

static void
smp_core99_kick_cpu(int nr)
{
	unsigned long save_vector, new_vector;
	unsigned long flags;
#if 1 /* New way... */
	volatile unsigned long *vector
		 = ((volatile unsigned long *)(KERNELBASE+0x100));
	if (nr < 1 || nr > 3)
		return;
#else
	volatile unsigned long *vector
		 = ((volatile unsigned long *)(KERNELBASE+0x500));
	if (nr != 1)
		return;
#endif
	if (ppc_md.progress) ppc_md.progress("smp_core99_kick_cpu", 0x346);

	local_irq_save(flags);
	local_irq_disable();
	
	/* Save reset vector */
	save_vector = *vector;
	
	/* Setup fake reset vector that does	  
	 *   b __secondary_start_psurge - KERNELBASE
	 */  
	switch(nr) {
		case 1:
			new_vector = (unsigned long)__secondary_start_psurge;
			break;
		case 2:
			new_vector = (unsigned long)__secondary_start_psurge2;
			break;
		case 3:
			new_vector = (unsigned long)__secondary_start_psurge3;
			break;
	}
	*vector = 0x48000002 + new_vector - KERNELBASE;
	
	/* flush data cache and inval instruction cache */
	flush_icache_range((unsigned long) vector, (unsigned long) vector + 4);
	
	/* Put some life in our friend */
	feature_core99_kick_cpu(nr);
	
	/* FIXME: We wait a bit for the CPU to take the exception, I should
	 * instead wait for the entry code to set something for me. Well,
	 * ideally, all that crap will be done in prom.c and the CPU left
	 * in a RAM-based wait loop like CHRP.
	 */
	mdelay(1);
	
	/* Restore our exception vector */
	*vector = save_vector;
	flush_icache_range((unsigned long) vector, (unsigned long) vector + 4);
	
	local_irq_restore(flags);
	if (ppc_md.progress) ppc_md.progress("smp_core99_kick_cpu done", 0x347);
}

static void
smp_core99_setup_cpu(int cpu_nr)
{
	/* Setup openpic */
	do_openpic_setup_cpu();

	/* Setup L2 */
	if (cpu_nr != 0)
		core99_init_l2();
	else
		if (ppc_md.progress) ppc_md.progress("core99_setup_cpu 0 done", 0x349);
}

static int
smp_chrp_probe(void)
{
	extern unsigned long smp_chrp_cpu_nr;

	if (smp_chrp_cpu_nr > 1)
		openpic_request_IPIs();

	return smp_chrp_cpu_nr;
}

static void
smp_chrp_kick_cpu(int nr)
{
	*(unsigned long *)KERNELBASE = nr;
	asm volatile("dcbf 0,%0"::"r"(KERNELBASE):"memory");
}

static void
smp_chrp_setup_cpu(int cpu_nr)
{
	static atomic_t ready = ATOMIC_INIT(1);
	static volatile int frozen = 0;

	if (cpu_nr == 0) {
		/* wait for all the others */
		while (atomic_read(&ready) < smp_num_cpus)
			barrier();
		atomic_set(&ready, 1);
		/* freeze the timebase */
		call_rtas("freeze-time-base", 0, 1, NULL);
		mb();
		frozen = 1;
		/* XXX assumes this is not a 601 */
		set_tb(0, 0);
		last_jiffy_stamp(0) = 0;
		while (atomic_read(&ready) < smp_num_cpus)
			barrier();
		/* thaw the timebase again */
		call_rtas("thaw-time-base", 0, 1, NULL);
		mb();
		frozen = 0;
		smp_tb_synchronized = 1;
	} else {
		atomic_inc(&ready);
		while (!frozen)
			barrier();
		set_tb(0, 0);
		last_jiffy_stamp(0) = 0;
		mb();
		atomic_inc(&ready);
		while (frozen)
			barrier();
	}

	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
}

#ifdef CONFIG_POWER4
static void
smp_xics_message_pass(int target, int msg, unsigned long data, int wait)
{
	/* for now, only do reschedule messages
	   since we only have one IPI */
	if (msg != PPC_MSG_RESCHEDULE)
		return;
	for (i = 0; i < smp_num_cpus; ++i) {
		if (target == MSG_ALL || target == i
		    || (target == MSG_ALL_BUT_SELF
			&& i != smp_processor_id()))
			xics_cause_IPI(i);
	}
}

static int
smp_xics_probe(void)
{
	return smp_chrp_cpu_nr;
}

static void
smp_xics_setup_cpu(int cpu_nr)
{
	if (cpu_nr > 0)
		xics_setup_cpu();
}
#endif /* CONFIG_POWER4 */

static int
smp_prep_probe(void)
{
	extern int mot_multi;

	if (mot_multi) {
		openpic_request_IPIs();
		smp_hw_index[1] = 1;
		return 2;
	}

	return 1;
}

static void
smp_prep_kick_cpu(int nr)
{
	extern unsigned long *MotSave_SmpIar;
	extern unsigned char *MotSave_CpusState[2];

	*MotSave_SmpIar = (unsigned long)__secondary_start_psurge - KERNELBASE;
	*MotSave_CpusState[1] = CPU_GOOD;
	printk("CPU1 reset, waiting\n");
}

static void
smp_prep_setup_cpu(int cpu_nr)
{
	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
}

#ifdef CONFIG_GEMINI	
static int
smp_gemini_probe(void)
{
	int i, nr;

        nr = (readb(GEMINI_CPUSTAT) & GEMINI_CPU_COUNT_MASK) >> 2;
	if (nr == 0)
		nr = 4;

	if (nr > 1) {
		openpic_request_IPIs();
		for (i = 1; i < nr; ++i)
			smp_hw_index[i] = i;
	}

	return nr;
}

static void
smp_gemini_kick_cpu(int nr)
{
	openpic_init_processor( 1<<i );
	openpic_init_processor( 0 );
}

static void
smp_gemini_setup_cpu(void)
{
	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
	if (cpu_nr > 0)
		gemini_init_l2();
}
#endif /* CONFIG_GEMINI */


static struct smp_ops_t {
	void  (*message_pass)(int target, int msg, unsigned long data, int wait);
	int   (*probe)(void);
	void  (*kick_cpu)(int nr);
	void  (*setup_cpu)(int nr);

} *smp_ops;

#define smp_message_pass(t,m,d,w) \
    do { if (smp_ops) \
	     atomic_inc(&ipi_sent); \
	     smp_ops->message_pass((t),(m),(d),(w)); \
       } while(0)


/* PowerSurge-style Macs */
static struct smp_ops_t psurge_smp_ops = {
	smp_psurge_message_pass,
	smp_psurge_probe,
	smp_psurge_kick_cpu,
	smp_psurge_setup_cpu,
};

/* Core99 Macs (dual G4s) */
static struct smp_ops_t core99_smp_ops = {
	smp_openpic_message_pass,
	smp_core99_probe,
	smp_core99_kick_cpu,
	smp_core99_setup_cpu,
};

/* CHRP with openpic */
static struct smp_ops_t chrp_smp_ops = {
	smp_openpic_message_pass,
	smp_chrp_probe,
	smp_chrp_kick_cpu,
	smp_chrp_setup_cpu,
};

#ifdef CONFIG_POWER4
/* CHRP with new XICS interrupt controller */
static struct smp_ops_t xics_smp_ops = {
	smp_xics_message_pass,
	smp_xics_probe,
	smp_chrp_kick_cpu,
	smp_xics_setup_cpu,
};
#endif /* CONFIG_POWER4 */

/* PReP (MTX) */
static struct smp_ops_t prep_smp_ops = {
	smp_openpic_message_pass,
	smp_prep_probe,
	smp_prep_kick_cpu,
	smp_prep_setup_cpu,
};

#ifdef CONFIG_GEMINI	
/* Gemini */
static struct smp_ops_t gemini_smp_ops = {
	smp_openpic_message_pass,
	smp_gemini_probe,
	smp_gemini_kick_cpu,
	smp_gemini_setup_cpu,
};
#endif /* CONFIG_GEMINI	*/

/* 
 * Common functions
 */
void smp_local_timer_interrupt(struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	if (!--prof_counter[cpu]) {
		update_process_times(user_mode(regs));
		prof_counter[cpu]=prof_multiplier[cpu];
	}
}

void smp_message_recv(int msg, struct pt_regs *regs)
{
	atomic_inc(&ipi_recv);
	
	switch( msg ) {
	case PPC_MSG_CALL_FUNCTION:
		smp_call_function_interrupt();
		break;
	case PPC_MSG_RESCHEDULE: 
		current->need_resched = 1;
		break;
	case PPC_MSG_INVALIDATE_TLB:
		_tlbia();
		break;
#ifdef CONFIG_XMON
	case PPC_MSG_XMON_BREAK:
		xmon(regs);
		break;
#endif /* CONFIG_XMON */
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

/*
 * 750's don't broadcast tlb invalidates so
 * we have to emulate that behavior.
 *   -- Cort
 */
void smp_send_tlb_invalidate(int cpu)
{
	if ( (_get_PVR()>>16) == 8 )
		smp_message_pass(MSG_ALL_BUT_SELF, PPC_MSG_INVALIDATE_TLB, 0, 0);
}

void smp_send_reschedule(int cpu)
{
	/*
	 * This is only used if `cpu' is running an idle task,
	 * so it will reschedule itself anyway...
	 *
	 * This isn't the case anymore since the other CPU could be
	 * sleeping and won't reschedule until the next interrupt (such
	 * as the timer).
	 *  -- Cort
	 */
	/* This is only used if `cpu' is running an idle task,
	   so it will reschedule itself anyway... */
	smp_message_pass(cpu, PPC_MSG_RESCHEDULE, 0, 0);
}

#ifdef CONFIG_XMON
void smp_send_xmon_break(int cpu)
{
	smp_message_pass(cpu, PPC_MSG_XMON_BREAK, 0, 0);
}
#endif /* CONFIG_XMON */

static void stop_this_cpu(void *dummy)
{
	__cli();
	while (1)
		;
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
	smp_num_cpus = 1;
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 * Stolen from the i386 version.
 */
static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;

static struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data;

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler, you may call it from a bottom half handler.
 */
{
	struct call_data_struct data;
	int ret = -1, cpus = smp_num_cpus-1;
	int timeout;

	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock_bh(&call_lock);
	call_data = &data;
	/* Send a message to all other CPUs and wait for them to respond */
	smp_message_pass(MSG_ALL_BUT_SELF, PPC_MSG_CALL_FUNCTION, 0, 0);

	/* Wait for response */
	timeout = 1000000;
	while (atomic_read(&data.started) != cpus) {
		if (--timeout == 0) {
			printk("smp_call_function on cpu %d: other cpus not responding (%d)\n",
			       smp_processor_id(), atomic_read(&data.started));
			goto out;
		}
		barrier();
		udelay(1);
	}

	if (wait) {
		timeout = 1000000;
		while (atomic_read(&data.finished) != cpus) {
			if (--timeout == 0) {
				printk("smp_call_function on cpu %d: other cpus not finishing (%d/%d)\n",
				       smp_processor_id(), atomic_read(&data.finished), atomic_read(&data.started));
				goto out;
			}
			barrier();
			udelay(1);
		}
	}
	ret = 0;

 out:
	spin_unlock_bh(&call_lock);
	return ret;
}

void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait)
		atomic_inc(&call_data->finished);
}

void __init smp_boot_cpus(void)
{
	extern struct task_struct *current_set[NR_CPUS];
	int i, cpu_nr;
	struct task_struct *p;
	unsigned long a;

	printk("Entering SMP Mode...\n");
	smp_num_cpus = 1;
        smp_store_cpu_info(0);

	/*
	 * assume for now that the first cpu booted is
	 * cpu 0, the master -- Cort
	 */
	cpu_callin_map[0] = 1;
	current->processor = 0;

	init_idle();

	for (i = 0; i < NR_CPUS; i++) {
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}

	/*
	 * XXX very rough, assumes 20 bus cycles to read a cache line,
	 * timebase increments every 4 bus cycles, 32kB L1 data cache.
	 */
	cacheflush_time = 5 * 1024;

	/* To be later replaced by some arch-specific routine */
	switch(_machine) {
	case _MACH_Pmac:
		/* Check for Core99 */
		if (find_devices("uni-n"))
			smp_ops = &core99_smp_ops;
		else
			smp_ops = &psurge_smp_ops;
		break;
	case _MACH_chrp:
#ifndef CONFIG_POWER4
		smp_ops = &chrp_smp_ops;
#else
		smp_ops = &xics_smp_ops;
#endif /* CONFIG_POWER4 */
		break;
	case _MACH_prep:
		smp_ops = &prep_smp_ops;
		break;
#ifdef CONFIG_GEMINI		
	case _MACH_gemini:
		smp_ops = &gemini_smp_ops;
		break;
#endif /* CONFIG_GEMINI	*/
	default:
		printk("SMP not supported on this machine.\n");
		return;
	}
	
	/* Probe arch for CPUs */
	cpu_nr = smp_ops->probe();

	/*
	 * only check for cpus we know exist.  We keep the callin map
	 * with cpus at the bottom -- Cort
	 */
	if (cpu_nr > max_cpus)
		cpu_nr = max_cpus;
	for (i = 1; i < cpu_nr; i++) {
		int c;
		struct pt_regs regs;
		
		/* create a process for the processor */
		/* we don't care about the values in regs since we'll
		   never reschedule the forked task. */
		/* We DO care about one bit in the pt_regs we
		   pass to do_fork.  That is the MSR_FP bit in 
		   regs.msr.  If that bit is on, then do_fork
		   (via copy_thread) will call giveup_fpu.
		   giveup_fpu will get a pointer to our (current's)
		   last register savearea via current->thread.regs 
		   and using that pointer will turn off the MSR_FP,
		   MSR_FE0 and MSR_FE1 bits.  At this point, this 
		   pointer is pointing to some arbitrary point within
		   our stack. */

		memset(&regs, 0, sizeof(struct pt_regs));
		
		if (do_fork(CLONE_VM|CLONE_PID, 0, &regs, 0) < 0)
			panic("failed fork for CPU %d", i);
		p = init_task.prev_task;
		if (!p)
			panic("No idle task for CPU %d", i);
		del_from_runqueue(p);
		unhash_process(p);
		init_tasks[i] = p;

		p->processor = i;
		p->has_cpu = 1;
		current_set[i] = p;

		/* need to flush here since secondary bats aren't setup */
		for (a = KERNELBASE; a < KERNELBASE + 0x800000; a += 32)
			asm volatile("dcbf 0,%0" : : "r" (a) : "memory");
		asm volatile("sync");

		/* wake up cpus */
		smp_ops->kick_cpu(i);
		
		/*
		 * wait to see if the cpu made a callin (is actually up).
		 * use this value that I found through experimentation.
		 * -- Cort
		 */
		for ( c = 1000; c && !cpu_callin_map[i] ; c-- )
			udelay(100);
		
		if ( cpu_callin_map[i] )
		{
			char buf[32];
			sprintf(buf, "found cpu %d", i);
			if (ppc_md.progress) ppc_md.progress(buf, 0x350+i);
			printk("Processor %d found.\n", i);
			smp_num_cpus++;
		} else {
			char buf[32];
			sprintf(buf, "didn't find cpu %d", i);
			if (ppc_md.progress) ppc_md.progress(buf, 0x360+i);
			printk("Processor %d is stuck.\n", i);
		}
	}

	/* Setup CPU 0 last (important) */
	smp_ops->setup_cpu(0);
	
	if (smp_num_cpus < 2)
		smp_tb_synchronized = 1;
}

void __init smp_software_tb_sync(int cpu)
{
#define PASSES 4	/* 4 passes.. */
	int pass;
	int i, j;

	/* stop - start will be the number of timebase ticks it takes for cpu0
	 * to send a message to all others and the first reponse to show up.
	 *
	 * ASSUMPTION: this time is similiar for all cpus
	 * ASSUMPTION: the time to send a one-way message is ping/2
	 */
	register unsigned long start = 0;
	register unsigned long stop = 0;
	register unsigned long temp = 0;

	set_tb(0, 0);

	/* multiple passes to get in l1 cache.. */
	for (pass = 2; pass < 2+PASSES; pass++){
		if (cpu == 0){
			mb();
			for (i = j = 1; i < smp_num_cpus; i++, j++){
				/* skip stuck cpus */
				while (!cpu_callin_map[j])
					++j;
				while (cpu_callin_map[j] != pass)
					barrier();
			}
			mb();
			tb_sync_flag = pass;
			start = get_tbl();	/* start timing */
			while (tb_sync_flag)
				mb();
			stop = get_tbl();	/* end timing */
			/* theoretically, the divisor should be 2, but
			 * I get better results on my dual mtx. someone
			 * please report results on other smp machines..
			 */
			tb_offset = (stop-start)/4;
			mb();
			tb_sync_flag = pass;
			udelay(10);
			mb();
			tb_sync_flag = 0;
			mb();
			set_tb(0,0);
			mb();
		} else {
			cpu_callin_map[cpu] = pass;
			mb();
			while (!tb_sync_flag)
				mb();		/* wait for cpu0 */
			mb();
			tb_sync_flag = 0;	/* send response for timing */
			mb();
			while (!tb_sync_flag)
				mb();
			temp = tb_offset;	/* make sure offset is loaded */
			while (tb_sync_flag)
				mb();
			set_tb(0,temp);		/* now, set the timebase */
			mb();
		}
	}
	if (cpu == 0) {
		smp_tb_synchronized = 1;
		printk("smp_software_tb_sync: %d passes, final offset: %ld\n",
			PASSES, tb_offset);
	}
	/* so time.c doesn't get confused */
	set_dec(tb_ticks_per_jiffy);
	last_jiffy_stamp(cpu) = 0;
	cpu_callin_map[cpu] = 1;
}

void __init smp_commence(void)
{
	/*
	 *	Lets the callin's below out of their loop.
	 */
	if (ppc_md.progress) ppc_md.progress("smp_commence", 0x370);
	wmb();
	smp_commenced = 1;

	/* if the smp_ops->setup_cpu function has not already synched the
	 * timebases with a nicer hardware-based method, do so now
	 *
	 * I am open to suggestions for improvements to this method
	 * -- Troy <hozer@drgw.net>
	 *
	 * NOTE: if you are debugging, set smp_tb_synchronized for now
	 * since if this code runs pretty early and needs all cpus that
	 * reported in in smp_callin_map to be working
	 *
	 * NOTE2: this code doesn't seem to work on > 2 cpus. -- paulus/BenH
	 */
	if (!smp_tb_synchronized && smp_num_cpus == 2) {
		unsigned long flags;
		__save_and_cli(flags);	
		smp_software_tb_sync(0);
		__restore_flags(flags);
	}
}

void __init smp_callin(void)
{
	int cpu = current->processor;
	
        smp_store_cpu_info(cpu);
	set_dec(tb_ticks_per_jiffy);
	cpu_callin_map[cpu] = 1;

	smp_ops->setup_cpu(cpu);

	init_idle();

	/*
	 * This cpu is now "online".  Only set them online
	 * before they enter the loop below since write access
	 * to the below variable is _not_ guaranteed to be
	 * atomic.
	 *   -- Cort <cort@fsmlabs.com>
	 */
	cpu_online_map |= 1UL << smp_processor_id();
	
	while(!smp_commenced)
		barrier();

	/* see smp_commence for more info */
	if (!smp_tb_synchronized && smp_num_cpus == 2) {
		smp_software_tb_sync(cpu);
	}
	__sti();
}

/* intel needs this */
void __init initialize_secondary(void)
{
}

/* Activate a secondary processor. */
int __init start_secondary(void *unused)
{
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	smp_callin();
	return cpu_idle(NULL);
}

void __init smp_setup(char *str, int *ints)
{
}

int __init setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

void __init smp_store_cpu_info(int id)
{
        struct cpuinfo_PPC *c = &cpu_data[id];

	/* assume bogomips are same for everything */
        c->loops_per_jiffy = loops_per_jiffy;
        c->pvr = _get_PVR();
}

static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);
