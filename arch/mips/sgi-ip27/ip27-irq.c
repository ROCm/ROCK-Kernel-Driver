/*
 * ip27-irq.c: Highlevel interrupt handling for IP27 architecture.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 1999 - 2001 Kanoj Sarcar
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/pci/bridge.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/addrs.h>
#include <asm/sn/agent.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/intr_public.h>

#undef DEBUG_IRQ
#ifdef DEBUG_IRQ
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/*
 * Number of levels in INT_PEND0. Can be set to 128 if we also
 * consider INT_PEND1.
 */
#define PERNODE_LEVELS	64

/*
 * we need to map irq's up to at least bit 7 of the INT_MASK0_A register
 * since bits 0-6 are pre-allocated for other purposes.
 */
#define FAST_IRQ_TO_LEVEL(i)	(i)
#define LEVEL_TO_IRQ(c, l) (node_level_to_irq[CPUID_TO_COMPACT_NODEID(c)][(l)])

/*
 * Linux has a controller-independent x86 interrupt architecture.
 * every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the apropriate
 * controller. Thus drivers need not be aware of the
 * interrupt-controller.
 *
 * Various interrupt controllers we handle: 8259 PIC, SMP IO-APIC,
 * PIIX4's internal 8259 PIC and SGI's Visual Workstation Cobalt (IO-)APIC.
 * (IO-APICs assumed to be messaging to Pentium local-APICs)
 *
 * the code is designed to be easily extended with new/different
 * interrupt controllers, without having to do assembly magic.
 */

extern asmlinkage void ip27_irq(void);

extern struct bridge_controller *irq_to_bridge[];
extern int irq_to_slot[];

/*
 * There is a single intpend register per node, and we want to have
 * distinct levels for intercpu intrs for both cpus A and B on a node.
 */
static int node_level_to_irq[MAX_COMPACT_NODES][PERNODE_LEVELS];

/*
 * use these macros to get the encoded nasid and widget id
 * from the irq value
 */
#define IRQ_TO_BRIDGE(i)		irq_to_bridge[(i)]
#define	SLOT_FROM_PCI_IRQ(i)		irq_to_slot[i]

static inline int alloc_level(cpuid_t cpunum, int irq)
{
	cnodeid_t nodenum = CPUID_TO_COMPACT_NODEID(cpunum);
	int j = BASE_PCI_IRQ;			/* pre-allocated entries */

	while (++j < PERNODE_LEVELS) {
		if (node_level_to_irq[nodenum][j] == -1) {
			node_level_to_irq[nodenum][j] = irq;
			return j;
		}
	}

	panic("Cpu %ld flooded with devices\n", cpunum);
}

static inline int find_level(cpuid_t *cpunum, int irq)
{
	int j;
	cnodeid_t nodenum = INVALID_CNODEID;

	while (++nodenum < MAX_COMPACT_NODES) {
		j = BASE_PCI_IRQ;		/* Pre-allocated entries */
		while (++j < PERNODE_LEVELS)
			if (node_level_to_irq[nodenum][j] == irq) {
				*cpunum = 0;	/* XXX Fixme */
				return(j);
			}
	}

	panic("Could not identify cpu/level for irq %d\n", irq);
}

/*
 * Find first bit set
 */
static int ms1bit(unsigned long x)
{
	int b = 0, s;

	s = 16; if (x >> 16 == 0) s = 0; b += s; x >>= s;
	s =  8; if (x >>  8 == 0) s = 0; b += s; x >>= s;
	s =  4; if (x >>  4 == 0) s = 0; b += s; x >>= s;
	s =  2; if (x >>  2 == 0) s = 0; b += s; x >>= s;
	s =  1; if (x >>  1 == 0) s = 0; b += s;

	return b;
}

/*
 * This code is unnecessarily complex, because we do SA_INTERRUPT
 * intr enabling. Basically, once we grab the set of intrs we need
 * to service, we must mask _all_ these interrupts; firstly, to make
 * sure the same intr does not intr again, causing recursion that
 * can lead to stack overflow. Secondly, we can not just mask the
 * one intr we are do_IRQing, because the non-masked intrs in the
 * first set might intr again, causing multiple servicings of the
 * same intr. This effect is mostly seen for intercpu intrs.
 * Kanoj 05.13.00
 */

void ip27_do_irq_mask0(struct pt_regs *regs)
{
	int irq, swlevel;
	hubreg_t pend0, mask0;
	cpuid_t cpu = smp_processor_id();
	int pi_int_mask0 =
		(cputoslice(cpu) == 0) ?  PI_INT_MASK0_A : PI_INT_MASK0_B;

	/* copied from Irix intpend0() */
	pend0 = LOCAL_HUB_L(PI_INT_PEND0);
	mask0 = LOCAL_HUB_L(pi_int_mask0);

	pend0 &= mask0;		/* Pick intrs we should look at */
	if (!pend0)
		return;

	/* Prevent any of the picked intrs from recursing */
	LOCAL_HUB_S(pi_int_mask0, mask0 & ~pend0);

	swlevel = ms1bit(pend0);
#ifdef CONFIG_SMP
	if (pend0 & (1UL << CPU_RESCHED_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_A_IRQ);
	} else if (pend0 & (1UL << CPU_RESCHED_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_B_IRQ);
	} else if (pend0 & (1UL << CPU_CALL_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_A_IRQ);
		smp_call_function_interrupt();
	} else if (pend0 & (1UL << CPU_CALL_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_B_IRQ);
		smp_call_function_interrupt();
	} else
#endif
	{
		/* "map" swlevel to irq */
		irq = LEVEL_TO_IRQ(cpu, swlevel);
		do_IRQ(irq, regs);
	}

	/* clear bit in pend0 */
	pend0 ^= 1UL << swlevel;

	/* Now allow the set of serviced intrs again */
	LOCAL_HUB_S(pi_int_mask0, mask0);
	LOCAL_HUB_L(PI_INT_PEND0);
}

void ip27_do_irq_mask1(struct pt_regs *regs)
{
	int irq, swlevel;
	hubreg_t pend1, mask1;
	cpuid_t cpu = smp_processor_id();
	int pi_int_mask1 = (cputoslice(cpu) == 0) ?  PI_INT_MASK1_A : PI_INT_MASK1_B;

	/* copied from Irix intpend0() */
	pend1 = LOCAL_HUB_L(PI_INT_PEND1);
	mask1 = LOCAL_HUB_L(pi_int_mask1);

	pend1 &= mask1;		/* Pick intrs we should look at */
	if (!pend1)
		return;

	/* Prevent any of the picked intrs from recursing */
	LOCAL_HUB_S(pi_int_mask1, mask1 & ~pend1);

	swlevel = ms1bit(pend1);
	/* "map" swlevel to irq */
	irq = LEVEL_TO_IRQ(cpu, swlevel);
	LOCAL_HUB_CLR_INTR(swlevel);
	do_IRQ(irq, regs);
	/* clear bit in pend1 */
	pend1 ^= 1UL << swlevel;

	/* Now allow the set of serviced intrs again */
	LOCAL_HUB_S(pi_int_mask1, mask1);
	LOCAL_HUB_L(PI_INT_PEND1);
}

void ip27_prof_timer(struct pt_regs *regs)
{
	panic("CPU %d got a profiling interrupt", smp_processor_id());
}

void ip27_hub_error(struct pt_regs *regs)
{
	panic("CPU %d got a hub error interrupt", smp_processor_id());
}

/*
 * Get values that vary depending on which CPU and bit we're operating on.
 */
static void intr_get_ptrs(cpuid_t cpu, int bit, int *new_bit,
                          hubreg_t **intpend_masks, int *ip)
{
	struct hub_intmasks_s *hub_intmasks = &cpu_data[cpu].p_intmasks;

	if (bit < N_INTPEND_BITS) {
		*intpend_masks = &hub_intmasks->intpend0_masks;
		*ip = 0;
		*new_bit = bit;
	} else {
		*intpend_masks = &hub_intmasks->intpend1_masks;
		*ip = 1;
		*new_bit = bit - N_INTPEND_BITS;
	}
}

static int intr_connect_level(int cpu, int bit)
{
	int ip;
	int slice = cputoslice(cpu);
	volatile hubreg_t *mask_reg;
	hubreg_t *intpend_masks;
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cputocnode(cpu));

	intr_get_ptrs(cpu, bit, &bit, &intpend_masks, &ip);

	/* Make sure it's not already pending when we connect it. */
	REMOTE_HUB_CLR_INTR(nasid, bit + ip * N_INTPEND_BITS);

	*intpend_masks |= (1UL << bit);

	if (ip == 0) {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK0_A +
		                PI_INT_MASK_OFFSET * slice);
	} else {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK1_A +
				PI_INT_MASK_OFFSET * slice);
	}
	HUB_S(mask_reg, intpend_masks[0]);

	return 0;
}

static int intr_disconnect_level(int cpu, int bit)
{
	int ip;
	int slice = cputoslice(cpu);
	volatile hubreg_t *mask_reg;
	hubreg_t *intpend_masks;
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cputocnode(cpu));

	intr_get_ptrs(cpu, bit, &bit, &intpend_masks, &ip);
	intpend_masks[0] &= ~(1ULL << (u64)bit);
	if (ip == 0) {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK0_A +
				PI_INT_MASK_OFFSET * slice);
	} else {
		mask_reg = REMOTE_HUB_ADDR(nasid, PI_INT_MASK1_A +
				PI_INT_MASK_OFFSET * slice);
	}
	HUB_S(mask_reg, intpend_masks[0]);

	return 0;
}

/* Startup one of the (PCI ...) IRQs routes over a bridge.  */
static unsigned int startup_bridge_irq(unsigned int irq)
{
	struct bridge_controller *bc;
	bridgereg_t device;
	bridge_t *bridge;
	int pin, swlevel;

	if (irq < BASE_PCI_IRQ)
		return 0;

	pin = SLOT_FROM_PCI_IRQ(irq);
	bc = IRQ_TO_BRIDGE(irq);
	bridge = bc->base;

	DBG("bridge_startup(): irq= 0x%x  pin=%d\n", irq, pin);
	/*
	 * "map" irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	swlevel = alloc_level(bc->irq_cpu, irq);
	intr_connect_level(bc->irq_cpu, swlevel);

	bridge->b_int_addr[pin].addr = (0x20000 | swlevel | (bc->nasid << 8));
	bridge->b_int_enable |= (1 << pin);
	/* more stuff in int_enable reg */
	bridge->b_int_enable |= 0x7ffffe00;

	/*
	 * Enable sending of an interrupt clear packt to the hub on a high to
	 * low transition of the interrupt pin.
	 *
	 * IRIX sets additional bits in the address which are documented as
	 * reserved in the bridge docs.
	 */
	bridge->b_int_mode |= (1UL << pin);

	/*
	 * We assume the bridge to have a 1:1 mapping between devices
	 * (slots) and intr pins.
	 */
	device = bridge->b_int_device;
	device &= ~(7 << (pin*3));
	device |= (pin << (pin*3));
	bridge->b_int_device = device;

        bridge->b_widget.w_tflush;                      /* Flush */

        return 0;       /* Never anything pending.  */
}

/* Shutdown one of the (PCI ...) IRQs routes over a bridge.  */
static void shutdown_bridge_irq(unsigned int irq)
{
	struct bridge_controller *bc = IRQ_TO_BRIDGE(irq);
	bridge_t *bridge = bc->base;
	int pin, swlevel;
	cpuid_t cpu;

	BUG_ON(irq < BASE_PCI_IRQ);

	DBG("bridge_shutdown: irq 0x%x\n", irq);
	pin = SLOT_FROM_PCI_IRQ(irq);

	/*
	 * map irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	swlevel = find_level(&cpu, irq);
	intr_disconnect_level(cpu, swlevel);
	LEVEL_TO_IRQ(cpu, swlevel) = -1;

	bridge->b_int_enable &= ~(1 << pin);
	bridge->b_widget.w_tflush;                      /* Flush */
}

static inline void enable_bridge_irq(unsigned int irq)
{
	/* All the braindamage happens magically for us in ip27_do_irq */
}

static void disable_bridge_irq(unsigned int irq)
{
	/* All the braindamage happens magically for us in ip27_do_irq */
}

static void mask_and_ack_bridge_irq(unsigned int irq)
{
	/* All the braindamage happens magically for us in ip27_do_irq */
}

static void end_bridge_irq(unsigned int irq)
{
}

static struct hw_interrupt_type bridge_irq_type = {
	.typename	= "bridge",
	.startup	= startup_bridge_irq,
	.shutdown	= shutdown_bridge_irq,
	.enable		= enable_bridge_irq,
	.disable	= disable_bridge_irq,
	.ack		= mask_and_ack_bridge_irq,
	.end		= end_bridge_irq,
};

void irq_debug(void)
{
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;

	printk("bridge->b_int_status = 0x%x\n", bridge->b_int_status);
	printk("bridge->b_int_enable = 0x%x\n", bridge->b_int_enable);
	printk("PI_INT_PEND0   = 0x%lx\n", LOCAL_HUB_L(PI_INT_PEND0));
	printk("PI_INT_MASK0_A = 0x%lx\n", LOCAL_HUB_L(PI_INT_MASK0_A));
}

void __init init_IRQ(void)
{
	int i, j;

	for (i = 0; i < MAX_COMPACT_NODES; i++)
		for (j = 0; j < PERNODE_LEVELS; j++)
			node_level_to_irq[i][j] = -1;

	set_except_vector(0, ip27_irq);

	/*
	 * Right now the bridge irq is our kitchen sink interrupt type
	 */
	for (i = 0; i <= NR_IRQS; i++) {
		irq_desc[i].status	= IRQ_DISABLED;
		irq_desc[i].action	= 0;
		irq_desc[i].depth	= 1;
		irq_desc[i].handler	= &bridge_irq_type;
	}
}

void install_ipi(void)
{
	int slice = LOCAL_HUB_L(PI_CPU_NUM);
	int cpu = smp_processor_id();
	hubreg_t mask, set;

	if (slice == 0) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_A_IRQ);
		LOCAL_HUB_CLR_INTR(CPU_CALL_A_IRQ);
		mask = LOCAL_HUB_L(PI_INT_MASK0_A);	/* Slice A */
		set = (1UL << FAST_IRQ_TO_LEVEL(CPU_RESCHED_A_IRQ)) |
		      (1UL << FAST_IRQ_TO_LEVEL(CPU_CALL_A_IRQ));
		mask |= set;
		cpu_data[cpu].p_intmasks.intpend0_masks |= set;
		LOCAL_HUB_S(PI_INT_MASK0_A, mask);
	} else {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_B_IRQ);
		LOCAL_HUB_CLR_INTR(CPU_CALL_B_IRQ);
		mask = LOCAL_HUB_L(PI_INT_MASK0_B);	/* Slice B */
		set = (1UL << FAST_IRQ_TO_LEVEL(CPU_RESCHED_B_IRQ)) |
		      (1UL << FAST_IRQ_TO_LEVEL(CPU_CALL_B_IRQ));
		mask |= set;
		cpu_data[cpu].p_intmasks.intpend0_masks |= set;
		LOCAL_HUB_S(PI_INT_MASK0_B, mask);
	}
}
