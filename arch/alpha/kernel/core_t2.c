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
mk_conf_addr(struct pci_dev *dev, int where, unsigned long *pci_addr,
	     unsigned char *type1)
{
	unsigned long addr;
	u8 bus = dev->bus->number;
	u8 device_fn = dev->devfn;

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

	__save_and_cli(flags);	/* avoid getting hit by machine check */

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

	__restore_flags(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int cpu;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	__save_and_cli(flags);	/* avoid getting hit by machine check */

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
	__restore_flags(flags);
}

static int
t2_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x00 + T2_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int 
t2_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x08 + T2_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int 
t2_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x18 + T2_CONF;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

static int 
t2_write_config(struct pci_dev *dev, int where, u32 value, long mask)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + mask + T2_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

static int 
t2_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return t2_write_config(dev, where, value, 0x00);
}

static int 
t2_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return t2_write_config(dev, where, value, 0x08);
}

static int 
t2_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return t2_write_config(dev, where, value, 0x18);
}

struct pci_ops t2_pci_ops = 
{
	read_byte:	t2_read_config_byte,
	read_word:	t2_read_config_word,
	read_dword:	t2_read_config_dword,
	write_byte:	t2_write_config_byte,
	write_word:	t2_write_config_word,
	write_dword:	t2_write_config_dword
};

void __init
t2_init_arch(void)
{
	struct pci_controler *hose;
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++) {
		mcheck_expected(i) = 0;
		mcheck_taken(i) = 0;
	}

#if 0
	{
	  /* Set up error reporting.  */
	  unsigned long t2_err;

	  t2_err = *(vulp)T2_IOCSR;
	  t2_err |= (0x1 << 7);   /* master abort */
	  *(vulp)T2_IOCSR = t2_err;
	  mb();
	}
#endif

	printk("t2_init: HBASE was 0x%lx\n", *(vulp)T2_HBASE);
#if 0
	printk("t2_init: WBASE1=0x%lx WMASK1=0x%lx TBASE1=0x%lx\n",
	       *(vulp)T2_WBASE1,
	       *(vulp)T2_WMASK1,
	       *(vulp)T2_TBASE1);
	printk("t2_init: WBASE2=0x%lx WMASK2=0x%lx TBASE2=0x%lx\n",
	       *(vulp)T2_WBASE2,
	       *(vulp)T2_WMASK2,
	       *(vulp)T2_TBASE2);
#endif

	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, window 2 is  disabled.  In the future, we may
	 * want to use it to do scatter/gather DMA. 
	 *
	 * Window 1 goes at 1 GB and is 1 GB large.
	 */

	/* WARNING!! must correspond to the DMA_WIN params!!! */
	*(vulp)T2_WBASE1 = 0x400807ffU;
	*(vulp)T2_WMASK1 = 0x3ff00000U;
	*(vulp)T2_TBASE1 = 0;

	*(vulp)T2_WBASE2 = 0x0;
	*(vulp)T2_HBASE = 0x0;

	/* Zero HAE.  */
	*(vulp)T2_HAE_1 = 0; mb();
	*(vulp)T2_HAE_2 = 0; mb();
	*(vulp)T2_HAE_3 = 0; mb();
#if 0
	*(vulp)T2_HAE_4 = 0; mb(); /* do not touch this */
#endif

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controler();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	hose->sparse_mem_base = T2_SPARSE_MEM - IDENT_ADDR;
	hose->dense_mem_base = T2_DENSE_MEM - IDENT_ADDR;
	hose->sparse_io_base = T2_IO - IDENT_ADDR;
	hose->dense_io_base = 0;

	hose->sg_isa = hose->sg_pci = NULL;
	__direct_map_base = 0x40000000;
	__direct_map_size = 0x40000000;
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
