/*
 *	linux/arch/alpha/kernel/sys_titan.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996, 1999 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *      Copyright (C) 1999, 2000 Jeff Wiedemeier
 *
 * Code supporting TITAN systems (EV6+TITAN), currently:
 *      Privateer
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_titan.h>
#include <asm/hwrpb.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"

/* Note mask bit is true for ENABLED irqs. */
static unsigned long cached_irq_mask;
/* Titan boards handle at most four CPUs.  */
static unsigned long cpu_irq_affinity[4] = { ~0UL, ~0UL, ~0UL, ~0UL };

spinlock_t titan_irq_lock = SPIN_LOCK_UNLOCKED;

static void
titan_update_irq_hw(unsigned long mask)
{
	register titan_cchip *cchip = TITAN_cchip;
	unsigned long isa_enable = 1UL << 55;
	register int bcpu = boot_cpuid;

#ifdef CONFIG_SMP
	register unsigned long cpm = cpu_present_mask;
	volatile unsigned long *dim0, *dim1, *dim2, *dim3;
	unsigned long mask0, mask1, mask2, mask3, dummy;

	mask &= ~isa_enable;
	mask0 = mask & cpu_irq_affinity[0];
	mask1 = mask & cpu_irq_affinity[1];
	mask2 = mask & cpu_irq_affinity[2];
	mask3 = mask & cpu_irq_affinity[3];

	if (bcpu == 0) mask0 |= isa_enable;
	else if (bcpu == 1) mask1 |= isa_enable;
	else if (bcpu == 2) mask2 |= isa_enable;
	else mask3 |= isa_enable;

	dim0 = &cchip->dim0.csr;
	dim1 = &cchip->dim1.csr;
	dim2 = &cchip->dim2.csr;
	dim3 = &cchip->dim3.csr;
	if ((cpm & 1) == 0) dim0 = &dummy;
	if ((cpm & 2) == 0) dim1 = &dummy;
	if ((cpm & 4) == 0) dim2 = &dummy;
	if ((cpm & 8) == 0) dim3 = &dummy;

	*dim0 = mask0;
	*dim1 = mask1;
	*dim2 = mask2;
	*dim3 = mask3;
	mb();
	*dim0;
	*dim1;
	*dim2;
	*dim3;
#else
	volatile unsigned long *dimB;
	if (bcpu == 0) dimB = &cchip->dim0.csr;
	else if (bcpu == 1) dimB = &cchip->dim1.csr;
	else if (bcpu == 2) dimB = &cchip->dim2.csr;
	else if (bcpu == 3) dimB = &cchip->dim3.csr;

	*dimB = mask | isa_enable;
	mb();
	*dimB;
#endif
}

static inline void
privateer_enable_irq(unsigned int irq)
{
	spin_lock(&titan_irq_lock);
	cached_irq_mask |= 1UL << (irq - 16);
	titan_update_irq_hw(cached_irq_mask);
	spin_unlock(&titan_irq_lock);
}

static inline void
privateer_disable_irq(unsigned int irq)
{
	spin_lock(&titan_irq_lock);
	cached_irq_mask &= ~(1UL << (irq - 16));
	titan_update_irq_hw(cached_irq_mask);
	spin_unlock(&titan_irq_lock);
}

static unsigned int
privateer_startup_irq(unsigned int irq)
{
	privateer_enable_irq(irq);
	return 0;	/* never anything pending */
}

static void
privateer_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		privateer_enable_irq(irq);
}

static void
cpu_set_irq_affinity(unsigned int irq, unsigned long affinity)
{
	int cpu;

	for (cpu = 0; cpu < 4; cpu++) {
		if (affinity & (1UL << cpu))
			cpu_irq_affinity[cpu] |= 1UL << irq;
		else
			cpu_irq_affinity[cpu] &= ~(1UL << irq);
	}

}

static void
privateer_set_affinity(unsigned int irq, unsigned long affinity)
{ 
	spin_lock(&titan_irq_lock);
	cpu_set_irq_affinity(irq - 16, affinity);
	titan_update_irq_hw(cached_irq_mask);
	spin_unlock(&titan_irq_lock);
}

static struct hw_interrupt_type privateer_irq_type = {
	typename:	"PRIVATEER",
	startup:	privateer_startup_irq,
	shutdown:	privateer_disable_irq,
	enable:		privateer_enable_irq,
	disable:	privateer_disable_irq,
	ack:		privateer_disable_irq,
	end:		privateer_end_irq,
	set_affinity:	privateer_set_affinity,
};

static void
privateer_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	printk("privateer_device_interrupt: NOT IMPLEMENTED YET!! \n");
}

static void 
privateer_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq;

	irq = (vector - 0x800) >> 4;
	handle_irq(irq, regs);
}


static void __init
init_titan_irqs(struct hw_interrupt_type * ops, int imin, int imax)
{
	long i;
	for(i = imin; i <= imax; ++i) {
		irq_desc[i].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i].handler = ops;
	}
}

static void __init
privateer_init_irq(void)
{
	extern asmlinkage void entInt(void);
	int cpu;

	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
	outb(0, DMA2_MASK_REG);

	if (alpha_using_srm)
		alpha_mv.device_interrupt = privateer_srm_device_interrupt;

	titan_update_irq_hw(0UL);

	init_i8259a_irqs();
	init_titan_irqs(&privateer_irq_type, 16, 63 + 16);
}

/*
 * Privateer PCI Fixup configuration.
 *
 * PCHIP 0 BUS 0 (Hose 0)
 *
 *     IDSEL	Dev	What
 *     -----	---	----
 *	18	 7	Embedded Southbridge
 *	19	 8	Slot 0 
 *	20	 9	Slot 1
 *	21	10	Slot 2 
 *	22	11	Slot 3
 *	23	12	Embedded HotPlug controller
 *	27	16	Embedded Southbridge IDE
 *	29	18     	Embedded Southbridge PMU
 *	31	20	Embedded Southbridge USB
 *
 * PCHIP 1 BUS 0 (Hose 1)
 *
 *     IDSEL	Dev	What
 *     -----	---	----
 *	12	 1	Slot 0
 * 	13	 2	Slot 1
 *	17	 6	Embedded hotPlug controller
 *
 * PCHIP 0 BUS 1 (Hose 2)
 *
 *     IDSEL	What
 *     -----	----
 *	NONE	AGP
 *
 * PCHIP 1 BUS 1 (Hose 3)
 *
 *     IDSEL	Dev	What
 *     -----	---	----
 *	12	 1	Slot 0
 * 	13	 2	Slot 1
 *	17	 6	Embedded hotPlug controller
 *
 * Summary @ TITAN_CSR_DIM0:
 * Bit      Meaning
 *  0-7     Unused
 *  8       PCHIP 0 BUS 1 YUKON (if present)
 *  9       PCHIP 1 BUS 1 YUKON
 * 10       PCHIP 1 BUS 0 YUKON
 * 11       PCHIP 0 BUS 0 YUKON
 * 12       PCHIP 0 BUS 0 SLOT 2 INT A
 * 13       PCHIP 0 BUS 0 SLOT 2 INT B
 * 14       PCHIP 0 BUS 0 SLOT 2 INT C
 * 15       PCHIP 0 BUS 0 SLOT 2 INT D
 * 16       PCHIP 0 BUS 0 SLOT 3 INT A
 * 17       PCHIP 0 BUS 0 SLOT 3 INT B
 * 18       PCHIP 0 BUS 0 SLOT 3 INT C
 * 19       PCHIP 0 BUS 0 SLOT 3 INT D
 * 20       PCHIP 0 BUS 0 SLOT 0 INT A
 * 21       PCHIP 0 BUS 0 SLOT 0 INT B
 * 22       PCHIP 0 BUS 0 SLOT 0 INT C
 * 23       PCHIP 0 BUS 0 SLOT 0 INT D
 * 24       PCHIP 0 BUS 0 SLOT 1 INT A
 * 25       PCHIP 0 BUS 0 SLOT 1 INT B
 * 26       PCHIP 0 BUS 0 SLOT 1 INT C
 * 27       PCHIP 0 BUS 0 SLOT 1 INT D
 * 28       PCHIP 1 BUS 0 SLOT 0 INT A
 * 29       PCHIP 1 BUS 0 SLOT 0 INT B
 * 30       PCHIP 1 BUS 0 SLOT 0 INT C
 * 31       PCHIP 1 BUS 0 SLOT 0 INT D
 * 32       PCHIP 1 BUS 0 SLOT 1 INT A
 * 33       PCHIP 1 BUS 0 SLOT 1 INT B
 * 34       PCHIP 1 BUS 0 SLOT 1 INT C
 * 35       PCHIP 1 BUS 0 SLOT 1 INT D
 * 36       PCHIP 1 BUS 1 SLOT 0 INT A
 * 37       PCHIP 1 BUS 1 SLOT 0 INT B
 * 38       PCHIP 1 BUS 1 SLOT 0 INT C
 * 39       PCHIP 1 BUS 1 SLOT 0 INT D
 * 40       PCHIP 1 BUS 1 SLOT 1 INT A
 * 41       PCHIP 1 BUS 1 SLOT 1 INT B
 * 42       PCHIP 1 BUS 1 SLOT 1 INT C
 * 43       PCHIP 1 BUS 1 SLOT 1 INT D
 * 44       AGP INT A
 * 45       AGP INT B
 * 46-47    Unused
 * 49       Reserved for Sleep mode
 * 50       Temperature Warning (optional)
 * 51       Power Warning (optional)
 * 52       Reserved
 * 53       South Bridge NMI
 * 54       South Bridge SMI INT
 * 55       South Bridge ISA Interrupt
 * 56-58    Unused
 * 59       PCHIP1_C_ERROR
 * 60       PCHIP0_C_ERROR 
 * 61       PCHIP1_H_ERROR
 * 62       PCHIP0_H_ERROR
 * 63       Reserved
 *
 */
static int __init
privateer_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	u8 irq;
	
	pcibios_read_config_byte(dev->bus->number,
				 dev->devfn,
				 PCI_INTERRUPT_LINE,
				 &irq);

	/* is it routed through ISA? */
	if ((irq & 0xF0) == 0xE0)
		return (int)irq;

	return (int)irq + 16;	/* HACK -- this better only be called once */
}

#ifdef CONFIG_VGA_HOSE
static struct pci_controler * __init
privateer_vga_hose_select(struct pci_controler *h1, struct pci_controler *h2)
{
	struct pci_controler *hose = h1;
	int agp1, agp2;

	/* which hose(s) are agp? */
	agp1 = (0 != (TITAN_agp & (1 << h1->index)));
	agp2 = (0 != (TITAN_agp & (1 << h2->index)));
       
	hose = h1;			/* default to h1 */
	if (agp1 ^ agp2) {
		if (agp2) hose = h2;	/* take agp if only one */
	} else if (h2->index < h1->index)
		hose = h2;		/* first hose if 2xpci or 2xagp */

	return hose;
}
#endif

static void __init
privateer_init_pci(void)
{
	common_init_pci();
	SMC669_Init(0);
#ifdef CONFIG_VGA_HOSE
	locate_and_init_vga(privateer_vga_hose_select);
#endif
}

void
privateer_machine_check(unsigned long vector, unsigned long la_ptr,
			struct pt_regs * regs)
{
	/* only handle system events here */
	if (vector != SCB_Q_SYSEVENT) 
		return titan_machine_check(vector, la_ptr, regs);

	/* it's a system event, handle it here */
	printk("PRIVATEER 680 Machine Check on CPU %d\n", smp_processor_id());
}


/*
 * The System Vectors
 */

struct alpha_machine_vector privateer_mv __initmv = {
	vector_name:		"PRIVATEER",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_TITAN_IO,
	DO_TITAN_BUS,
	machine_check:		privateer_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,
	min_io_address:		DEFAULT_IO_BASE,
	min_mem_address:	DEFAULT_MEM_BASE,

	nr_irqs:		80,	/* 64 + 16 */
	device_interrupt:	privateer_device_interrupt,

	init_arch:		titan_init_arch,
	init_irq:		privateer_init_irq,
	init_rtc:		common_init_rtc,
	init_pci:		privateer_init_pci,
	kill_arch:		titan_kill_arch,
	pci_map_irq:		privateer_map_irq,
	pci_swizzle:		common_swizzle,
};
ALIAS_MV(privateer)
