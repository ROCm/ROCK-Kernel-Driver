/*
 *	linux/arch/alpha/kernel/core_titan.c
 *
 * Code common to all TITAN core logic chips.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/smp.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_titan.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "pci_impl.h"

unsigned TITAN_agp = 0;

static struct
{
	unsigned long wsba[4];
	unsigned long wsm[4];
	unsigned long tba[4];
} saved_pachip_port[4];

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_MCHECK 0  /* 0 = minimum, 1 = debug, 2 = dump+dump */
#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Note that all config space accesses use Type 1 address format.
 *
 * Note also that type 1 is determined by non-zero bus number.
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
	struct pci_controler *hose = dev->sysdata;
	unsigned long addr;
	u8 bus = dev->bus->number;
	u8 device_fn = dev->devfn;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

        if (hose->first_busno == dev->bus->number)
		bus = 0;
        *type1 = (bus != 0);

        addr = (bus << 16) | (device_fn << 8) | where;
	addr |= hose->config_space_base;
		
	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int 
titan_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldbu(*(vucp)addr);
	return PCIBIOS_SUCCESSFUL;
}

static int
titan_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldwu(*(vusp)addr);
	return PCIBIOS_SUCCESSFUL;
}

static int
titan_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *(vuip)addr;
	return PCIBIOS_SUCCESSFUL;
}

static int 
titan_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	__kernel_stb(value, *(vucp)addr);
	mb();
	__kernel_ldbu(*(vucp)addr);
	return PCIBIOS_SUCCESSFUL;
}

static int 
titan_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	__kernel_stw(value, *(vusp)addr);
	mb();
	__kernel_ldwu(*(vusp)addr);
	return PCIBIOS_SUCCESSFUL;
}

static int
titan_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*(vuip)addr = value;
	mb();
	*(vuip)addr;
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops titan_pci_ops = 
{
	read_byte:	titan_read_config_byte,
	read_word:	titan_read_config_word,
	read_dword:	titan_read_config_dword,
	write_byte:	titan_write_config_byte,
	write_word:	titan_write_config_word,
	write_dword:	titan_write_config_dword
};


void
titan_pci_tbi(struct pci_controler *hose, dma_addr_t start, dma_addr_t end)
{
	titan_pachip *pachip = 
	  (hose->index & 1) ? TITAN_pachip1 : TITAN_pachip0;
	titan_pachip_port *port;
	volatile unsigned long *csr;
	unsigned long value;

	/* Get the right hose */
	port = &pachip->g_port;
	if (hose->index & 2) 
		port = &pachip->a_port;

	/* We can invalidate up to 8 tlb entries in a go.  The flush
	   matches against <31:16> in the pci address.  */
	csr = &port->port_specific.g.gtlbia.csr;
	if (((start ^ end) & 0xffff0000) == 0)
		csr = &port->port_specific.g.gtlbiv.csr;

	/* For TBIA, it doesn't matter what value we write.  For TBI, 
	   it's the shifted tag bits.  */
	value = (start & 0xffff0000) >> 12;

	wmb();
	*csr = value;
	mb();
	*csr;
}

#define FN __FUNCTION__

static int __init
titan_query_agp(titan_pachip_port *port)
{
	union TPAchipPCTL pctl;

	/* set up APCTL */
	pctl.pctl_q_whole = port->pctl.csr;

	return pctl.pctl_r_bits.apctl_v_agp_present;

}
static void __init
titan_init_agp(titan_pachip_port *port, struct pci_controler *hose)
{
	union TPAchipPCTL pctl;

	if (!titan_query_agp(port))
		return;

	printk("AGP present on hose %d\n", hose->index);

	/* get APCTL */
	pctl.pctl_q_whole = port->pctl.csr;


	pctl.pctl_r_bits.apctl_v_agp_en = 1;	/* enable AGP */
	pctl.pctl_r_bits.apctl_v_agp_lp_rd = 0;	
	pctl.pctl_r_bits.apctl_v_agp_hp_rd = 0;	

	port->pctl.csr = pctl.pctl_q_whole;

	TITAN_agp |= 1 << hose->index;

#ifdef CONFIG_VGA_HOSE	
	/* is a graphics card on the AGP? (always device 5) */
	if (hose != NULL &&
	    __kernel_ldwu(*(vusp)(hose->config_space_base + 0x280a)) == 
	    PCI_CLASS_DISPLAY_VGA)
		set_vga_hose(hose);
#endif
}

static void __init
titan_init_one_pachip_port(titan_pachip_port *port, int index)
{
	struct pci_controler *hose;

	hose = alloc_pci_controler();
	if (index == 0)
		pci_isa_hose = hose;
	hose->io_space = alloc_resource();
	hose->mem_space = alloc_resource();

	/* This is for userland consumption.  For some reason, the 40-bit
	   PIO bias that we use in the kernel through KSEG didn't work for
	   the page table based user mappings.  So make sure we get the
	   43-bit PIO bias.  */
	hose->sparse_mem_base = 0;
	hose->sparse_io_base = 0;
	hose->dense_mem_base
	  = (TITAN_MEM(index) & 0xffffffffff) | 0x80000000000;
	hose->dense_io_base
	  = (TITAN_IO(index) & 0xffffffffff) | 0x80000000000;

	hose->config_space_base = TITAN_CONF(index);
	hose->index = index;

	hose->io_space->start = TITAN_IO(index) - TITAN_IO_BIAS;
	hose->io_space->end = hose->io_space->start + TITAN_IO_SPACE - 1;
	hose->io_space->name = pci_io_names[index];
	hose->io_space->flags = IORESOURCE_IO;

	hose->mem_space->start = TITAN_MEM(index) - TITAN_MEM_BIAS;
	hose->mem_space->end = hose->mem_space->start + 0xffffffff;
	hose->mem_space->name = pci_mem_names[index];
	hose->mem_space->flags = IORESOURCE_MEM;

	if (request_resource(&ioport_resource, hose->io_space) < 0)
		printk(KERN_ERR "Failed to request IO on hose %d\n", index);
	if (request_resource(&iomem_resource, hose->mem_space) < 0)
		printk(KERN_ERR "Failed to request MEM on hose %d\n", index);

	/* It's safe to call this for both G-Ports and A-Ports */
	titan_init_agp(port, hose);

	/*
	 * Save the existing PCI window translations.  SRM will 
	 * need them when we go to reboot.
	 */
	saved_pachip_port[index].wsba[0] = port->wsba[0].csr;
	saved_pachip_port[index].wsm[0]  = port->wsm[0].csr;
	saved_pachip_port[index].tba[0]  = port->tba[0].csr;

	saved_pachip_port[index].wsba[1] = port->wsba[1].csr;
	saved_pachip_port[index].wsm[1]  = port->wsm[1].csr;
	saved_pachip_port[index].tba[1]  = port->tba[1].csr;

	saved_pachip_port[index].wsba[2] = port->wsba[2].csr;
	saved_pachip_port[index].wsm[2]  = port->wsm[2].csr;
	saved_pachip_port[index].tba[2]  = port->tba[2].csr;

	saved_pachip_port[index].wsba[3] = port->wsba[3].csr;
	saved_pachip_port[index].wsm[3]  = port->wsm[3].csr;
	saved_pachip_port[index].tba[3]  = port->tba[3].csr;

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Note: Window 3 on Titan is Scatter-Gather ONLY
	 *
	 * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is direct access 1GB at 1GB
	 * Window 2 is direct access 1GB at 2GB
	 * Window 3 is scatter-gather 128MB at 3GB
	 * ??? We ought to scale window 3 memory.
	 *
	 * We must actually use 2 windows to direct-map the 2GB space,
	 * because of an idiot-syncrasy of the CYPRESS chip.  It may
	 * respond to a PCI bus address in the last 1MB of the 4GB
	 * address range.
	 */
	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 0);
	hose->sg_isa->align_entry = 8; /* 64KB for ISA */

	hose->sg_pci = iommu_arena_new(hose, 0xc0000000, 0x08000000, 0);
	hose->sg_pci->align_entry = 4; /* Titan caches 4 PTEs at a time */

	__direct_map_base = 0x40000000;
	__direct_map_size = 0x80000000;

	port->wsba[0].csr = hose->sg_isa->dma_base | 3;
	port->wsm[0].csr  = (hose->sg_isa->size - 1) & 0xfff00000;
	port->tba[0].csr  = virt_to_phys(hose->sg_isa->ptes);

	port->wsba[1].csr = 0x40000000 | 1;
	port->wsm[1].csr  = (0x40000000 - 1) & 0xfff00000;
	port->tba[1].csr  = 0;

	port->wsba[2].csr = 0x80000000 | 1;
	port->wsm[2].csr  = (0x40000000 - 1) & 0xfff00000;
	port->tba[2].csr  = 0x40000000;

	port->wsba[3].csr = hose->sg_pci->dma_base | 3;
	port->wsm[3].csr  = (hose->sg_pci->size - 1) & 0xfff00000;
	port->tba[3].csr  = virt_to_phys(hose->sg_pci->ptes);

	titan_pci_tbi(hose, 0, -1);
}

static void __init
titan_init_pachips(titan_pachip *pachip0, titan_pachip *pachip1)
{
	int pchip1_present = TITAN_cchip->csc.csr & 1L<<14;

	/* Init the ports in hose order... */
	titan_init_one_pachip_port(&pachip0->g_port, 0);	/* hose 0 */
	if (pchip1_present)
		titan_init_one_pachip_port(&pachip1->g_port, 1);/* hose 1 */
	titan_init_one_pachip_port(&pachip0->a_port, 2);	/* hose 2 */
	if (pchip1_present)
		titan_init_one_pachip_port(&pachip1->a_port, 3);/* hose 3 */
}

void __init
titan_init_arch(void)
{
#if 0
	printk("%s: titan_init_arch()\n", FN);
	printk("%s: CChip registers:\n", FN);
	printk("%s: CSR_CSC 0x%lx\n", FN, TITAN_cchip->csc.csr);
	printk("%s: CSR_MTR 0x%lx\n", FN, TITAN_cchip->mtr.csr);
	printk("%s: CSR_MISC 0x%lx\n", FN, TITAN_cchip->misc.csr);
	printk("%s: CSR_DIM0 0x%lx\n", FN, TITAN_cchip->dim0.csr);
	printk("%s: CSR_DIM1 0x%lx\n", FN, TITAN_cchip->dim1.csr);
	printk("%s: CSR_DIR0 0x%lx\n", FN, TITAN_cchip->dir0.csr);
	printk("%s: CSR_DIR1 0x%lx\n", FN, TITAN_cchip->dir1.csr);
	printk("%s: CSR_DRIR 0x%lx\n", FN, TITAN_cchip->drir.csr);

	printk("%s: DChip registers:\n", FN);
	printk("%s: CSR_DSC 0x%lx\n", FN, TITAN_dchip->dsc.csr);
	printk("%s: CSR_STR 0x%lx\n", FN, TITAN_dchip->str.csr);
	printk("%s: CSR_DREV 0x%lx\n", FN, TITAN_dchip->drev.csr);
#endif

	boot_cpuid = __hard_smp_processor_id();

	/* With multiple PCI busses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;
	iomem_resource.end = ~0UL;

	/* Init the PA chip(s) */
	titan_init_pachips(TITAN_pachip0, TITAN_pachip1);
}

static void
titan_kill_one_pachip_port(titan_pachip_port *port, int index)
{
	port->wsba[0].csr = saved_pachip_port[index].wsba[0];
	port->wsm[0].csr  = saved_pachip_port[index].wsm[0];
	port->tba[0].csr  = saved_pachip_port[index].tba[0];

	port->wsba[1].csr = saved_pachip_port[index].wsba[1];
	port->wsm[1].csr  = saved_pachip_port[index].wsm[1];
	port->tba[1].csr  = saved_pachip_port[index].tba[1];

	port->wsba[2].csr = saved_pachip_port[index].wsba[2];
	port->wsm[2].csr  = saved_pachip_port[index].wsm[2];
	port->tba[2].csr  = saved_pachip_port[index].tba[2];

	port->wsba[3].csr = saved_pachip_port[index].wsba[3];
	port->wsm[3].csr  = saved_pachip_port[index].wsm[3];
	port->tba[3].csr  = saved_pachip_port[index].tba[3];
}

static void
titan_kill_pachips(titan_pachip *pachip0, titan_pachip *pachip1)
{
	int pchip1_present = TITAN_cchip->csc.csr & 1L<<14;

	if (pchip1_present) {
		titan_kill_one_pachip_port(&pachip0->g_port, 1);
		titan_kill_one_pachip_port(&pachip0->a_port, 3);
	}
	titan_kill_one_pachip_port(&pachip0->g_port, 0);
	titan_kill_one_pachip_port(&pachip0->a_port, 2);
}

void
titan_kill_arch(int mode)
{
	titan_kill_pachips(TITAN_pachip0, TITAN_pachip1);
}

static inline void
titan_pci_clr_err_1(titan_pachip *pachip)
{
	unsigned int jd;

	jd = pachip->g_port.port_specific.g.gperror.csr;
	pachip->g_port.port_specific.g.gperror.csr = jd;
	mb();
	pachip->g_port.port_specific.g.gperror.csr;
}

static inline void
titan_pci_clr_err(void)
{
	titan_pci_clr_err_1(TITAN_pachip0);

	if (TITAN_cchip->csc.csr & 1L<<14)
	    titan_pci_clr_err_1(TITAN_pachip1);
}

void
titan_machine_check(unsigned long vector, unsigned long la_ptr,
		      struct pt_regs * regs)
{
	/* clear error before any reporting. */
	mb();
	draina();
	titan_pci_clr_err();
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, regs, "TITAN",
			    mcheck_expected(smp_processor_id()));
}

