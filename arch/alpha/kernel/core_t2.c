/*
 *	linux/arch/alpha/kernel/core_t2.c
 *
 * Written by Jay A Estabrook (jestabro@amt.tay1.dec.com).
 * December 1996.
 *
 * based on CIA code by David A Rusling (david.rusling@reo.mts.dec.com)
 *
 * Code common to all T2 core logic chips.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>

#define __EXTERN_INLINE
#include <asm/io.h>
#include <asm/core_t2.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "pci_impl.h"

/*
 * By default, we direct-map starting at 2GB, in order to allow the
 * maximum size direct-map window (2GB) to match the maximum amount of
 * memory (2GB) that can be present on SABLEs. But that limits the
 * floppy to DMA only via the scatter/gather window set up for 8MB
 * ISA DMA, since the maximum ISA DMA address is 2GB-1.
 *
 * For now, this seems a reasonable trade-off: even though most SABLEs
 * have far less than even 1GB of memory, floppy usage/performance will
 * not really be affected by forcing it to go via scatter/gather...
 */
#define T2_DIRECTMAP_2G 1

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG(args)	printk args
#else
# define DBG(args)
#endif


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the T2_HAXR2 register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	The function number selects which function of a multi-function device 
 *	(e.g., SCSI and Ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static int
mk_conf_addr(struct pci_bus *pbus, unsigned int device_fn, int where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	unsigned long addr;
	u8 bus = pbus->number;

	DBG(("mk_conf_addr(bus=%d, dfn=0x%x, where=0x%x,"
	     " addr=0x%lx, type1=0x%x)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* Type 0 configuration cycle.  */

		if (device > 8) {
			DBG(("mk_conf_addr: device (%d)>20, returning -1\n",
			     device));
			return -1;
		}

		*type1 = 0;
		addr = (0x0800L << device) | ((device_fn & 7) << 8) | (where);
	} else {
		/* Type 1 configuration cycle.  */
		*type1 = 1;
		addr = (bus << 16) | (device_fn << 8) | (where);
	}
	*pci_addr = addr;
	DBG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int value, cpu;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	local_irq_save(flags);	/* avoid getting hit by machine check */

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

#if 0
	{
	  unsigned long stat0;
	  /* Reset status register to avoid losing errors.  */
	  stat0 = *(vulp)T2_IOCSR;
	  *(vulp)T2_IOCSR = stat0;
	  mb();
	  DBG(("conf_read: T2 IOCSR was 0x%x\n", stat0));
	}
#endif

	/* If Type1 access, must set T2 CFG.  */
	if (type1) {
		t2_cfg = *(vulp)T2_HAE_3 & ~0xc0000000UL;
		*(vulp)T2_HAE_3 = 0x40000000UL | t2_cfg;
		mb();
		DBG(("conf_read: TYPE1 access\n"));
	}
	mb();
	draina();

	mcheck_expected(cpu) = 1;
	mcheck_taken(cpu) = 0;
	mb();

	/* Access configuration space. */
	value = *(vuip)addr;
	mb();
	mb();  /* magic */

	if (mcheck_taken(cpu)) {
		mcheck_taken(cpu) = 0;
		value = 0xffffffffU;
		mb();
	}
	mcheck_expected(cpu) = 0;
	mb();

	/* If Type1 access, must reset T2 CFG so normal IO space ops work.  */
	if (type1) {
		*(vulp)T2_HAE_3 = t2_cfg;
		mb();
	}
	DBG(("conf_read(): finished\n"));

	local_irq_restore(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int cpu;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	local_irq_save(flags);	/* avoid getting hit by machine check */

#if 0
	{
	  unsigned long stat0;
	  /* Reset status register to avoid losing errors.  */
	  stat0 = *(vulp)T2_IOCSR;
	  *(vulp)T2_IOCSR = stat0;
	  mb();
	  DBG(("conf_write: T2 ERR was 0x%x\n", stat0));
	}
#endif

	/* If Type1 access, must set T2 CFG.  */
	if (type1) {
		t2_cfg = *(vulp)T2_HAE_3 & ~0xc0000000UL;
		*(vulp)T2_HAE_3 = t2_cfg | 0x40000000UL;
		mb();
		DBG(("conf_write: TYPE1 access\n"));
	}
	mb();
	draina();

	mcheck_expected(cpu) = 1;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */

	mcheck_expected(cpu) = 0;
	mb();

	/* If Type1 access, must reset T2 CFG so normal IO space ops work.  */
	if (type1) {
		*(vulp)T2_HAE_3 = t2_cfg;
		mb();
	}
	DBG(("conf_write(): finished\n"));
	local_irq_restore(flags);
}

static int
t2_read_config(struct pci_bus *bus, unsigned int devfn, int where,
	       int size, u32 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;
	int shift;
	long mask;

	if (mk_conf_addr(bus, devfn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	shift = (where & 3) * 8;
	addr = (pci_addr << 5) + mask + T2_CONF;
	*value = conf_read(addr, type1) >> (shift);
	return PCIBIOS_SUCCESSFUL;
}

static int 
t2_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
		u32 value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;
	long mask;

	if (mk_conf_addr(bus, devfn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	addr = (pci_addr << 5) + mask + T2_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops t2_pci_ops = 
{
	.read =		t2_read_config,
	.write =	t2_write_config,
};

void __init
t2_init_arch(void)
{
	struct pci_controller *hose;
	unsigned long t2_iocsr;
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++) {
		mcheck_expected(i) = 0;
		mcheck_taken(i) = 0;
	}

#if 0
	/* Set up error reporting.  */
	t2_iocsr = *(vulp)T2_IOCSR;
	*(vulp)T2_IOCSR = t2_iocsr | (0x1UL << 7); /* TLB error check */
	mb();
	*(vulp)T2_IOCSR; /* read it back to make sure */
#endif

	/* Enable scatter/gather TLB use.  */
	t2_iocsr = *(vulp)T2_IOCSR;
	if (!(t2_iocsr & (0x1UL << 26))) {
		printk("t2_init_arch: enabling SG TLB, IOCSR was 0x%lx\n",
		       t2_iocsr);
		*(vulp)T2_IOCSR = t2_iocsr | (0x1UL << 26);
		mb();	
		*(vulp)T2_IOCSR; /* read it back to make sure */
	}

#if 0
	printk("t2_init_arch: HBASE was 0x%lx\n", *(vulp)T2_HBASE);
	printk("t2_init_arch: WBASE1=0x%lx WMASK1=0x%lx TBASE1=0x%lx\n",
	       *(vulp)T2_WBASE1, *(vulp)T2_WMASK1, *(vulp)T2_TBASE1);
	printk("t2_init_arch: WBASE2=0x%lx WMASK2=0x%lx TBASE2=0x%lx\n",
	       *(vulp)T2_WBASE2, *(vulp)T2_WMASK2, *(vulp)T2_TBASE2);
#endif

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controller();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	hose->sparse_mem_base = T2_SPARSE_MEM - IDENT_ADDR;
	hose->dense_mem_base = T2_DENSE_MEM - IDENT_ADDR;
	hose->sparse_io_base = T2_IO - IDENT_ADDR;
	hose->dense_io_base = 0;

	/* Note we can only do 1 SG window, as the other is for direct, so
	   do an ISA SG area, especially for the floppy. */
        hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 0);
	hose->sg_pci = NULL;

	/*
	 * Set up the PCI->physical memory translation windows.
	 *
	 * Window 1 goes at ? GB and is ?GB large, direct mapped.
	 * Window 2 goes at 8 MB and is 8MB large, scatter/gather (for ISA).
	 */

#if T2_DIRECTMAP_2G
	__direct_map_base = 0x80000000UL;
	__direct_map_size = 0x80000000UL;

	/* WARNING!! must correspond to the direct map window params!!! */
	*(vulp)T2_WBASE1 = 0x80080fffU;
	*(vulp)T2_WMASK1 = 0x7ff00000U;
	*(vulp)T2_TBASE1 = 0;
#else /* T2_DIRECTMAP_2G */
	__direct_map_base = 0x40000000UL;
	__direct_map_size = 0x40000000UL;

	/* WARNING!! must correspond to the direct map window params!!! */
	*(vulp)T2_WBASE1 = 0x400807ffU;
	*(vulp)T2_WMASK1 = 0x3ff00000U;
	*(vulp)T2_TBASE1 = 0;
#endif /* T2_DIRECTMAP_2G */

	/* WARNING!! must correspond to the SG arena/window params!!! */
	*(vulp)T2_WBASE2 = 0x008c000fU;
	*(vulp)T2_WMASK2 = 0x00700000U;
	*(vulp)T2_TBASE2 = virt_to_phys(hose->sg_isa->ptes) >> 1;

	*(vulp)T2_HBASE = 0x0;

	/* Zero HAE.  */
	*(vulp)T2_HAE_1 = 0; mb();
	*(vulp)T2_HAE_2 = 0; mb();
	*(vulp)T2_HAE_3 = 0; mb();
#if 0
	*(vulp)T2_HAE_4 = 0; mb(); /* DO NOT TOUCH THIS!!! */
#endif

	t2_pci_tbi(hose, 0, -1); /* flush TLB all */
}

void
t2_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	unsigned long t2_iocsr;

	t2_iocsr = *(vulp)T2_IOCSR;

	/* set the TLB Clear bit */
	*(vulp)T2_IOCSR = t2_iocsr | (0x1UL << 28);
	mb();
	*(vulp)T2_IOCSR; /* read it back to make sure */

	/* clear the TLB Clear bit */
	*(vulp)T2_IOCSR = t2_iocsr & ~(0x1UL << 28);
	mb();
	*(vulp)T2_IOCSR; /* read it back to make sure */
}

#define SIC_SEIC (1UL << 33)    /* System Event Clear */

static void
t2_clear_errors(int cpu)
{
	struct sable_cpu_csr *cpu_regs;

	cpu_regs = (struct sable_cpu_csr *)T2_CPU0_BASE;
	if (cpu == 1)
		cpu_regs = (struct sable_cpu_csr *)T2_CPU1_BASE;
	if (cpu == 2)
		cpu_regs = (struct sable_cpu_csr *)T2_CPU2_BASE;
	if (cpu == 3)
		cpu_regs = (struct sable_cpu_csr *)T2_CPU3_BASE;
		
	cpu_regs->sic &= ~SIC_SEIC;

	/* Clear CPU errors.  */
	cpu_regs->bcce |= cpu_regs->bcce;
	cpu_regs->cbe  |= cpu_regs->cbe;
	cpu_regs->bcue |= cpu_regs->bcue;
	cpu_regs->dter |= cpu_regs->dter;

	*(vulp)T2_CERR1 |= *(vulp)T2_CERR1;
	*(vulp)T2_PERR1 |= *(vulp)T2_PERR1;

	mb();
	mb();  /* magic */
}

void
t2_machine_check(unsigned long vector, unsigned long la_ptr,
		 struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	/* Clear the error before any reporting.  */
	mb();
	mb();  /* magic */
	draina();
	t2_clear_errors(cpu);
	wrmces(rdmces()|1);	/* ??? */
	mb();

	process_mcheck_info(vector, la_ptr, regs, "T2", mcheck_expected(cpu));
}
