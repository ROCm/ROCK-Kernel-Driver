/*
 *	linux/arch/alpha/kernel/core_irongate.c
 *
 * Based on code written by David A. Rusling (david.rusling@reo.mts.dec.com).
 *
 *	Copyright (C) 1999 Alpha Processor, Inc.,
 *		(David Daniel, Stig Telfer, Soohoon Lee)
 *
 * Code common to all IRONGATE core logic chips.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pci.h>
#include <asm/hwrpb.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_irongate.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "pci_impl.h"

#undef DEBUG_IRONGATE 		/* define to enable verbose Irongate debug */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address accordingly.  It is therefore not safe
 * to have concurrent invocations to configuration space access
 * routines, but there really shouldn't be any need for this.
 *
 *	addr[31:24]		reserved
 *	addr[23:16]		bus number (8 bits = 128 possible buses)
 *	addr[15:11]		Device number (5 bits)
 *	addr[10: 8]		function number
 *	addr[ 7: 2]		register number
 *
 * For IRONGATE:
 *    if (bus = addr[23:16]) == 0
 *    then
 *	  type 0 config cycle:
 *	      addr_on_pci[31:11] = id selection for device = addr[15:11]
 *	      addr_on_pci[10: 2] = addr[10: 2] ???
 *	      addr_on_pci[ 1: 0] = 00
 *    else
 *	  type 1 config cycle (pass on with no decoding):
 *	      addr_on_pci[31:24] = 0
 *	      addr_on_pci[23: 2] = addr[23: 2]
 *	      addr_on_pci[ 1: 0] = 01
 *    fi
 *
 * Notes:
 *	The function number selects which function of a multi-function device
 *	(e.g., SCSI and Ethernet).
 *
 *	The register selects a DWORD (32 bit) register offset.	Hence it
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

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

	*type1 = (bus != 0);

	addr = (bus << 16) | (device_fn << 8) | where;
	addr |= IRONGATE_CONF;

	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int
irongate_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldbu(*(vucp)addr);
	return PCIBIOS_SUCCESSFUL;
}

static int
irongate_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldwu(*(vusp)addr);
	return PCIBIOS_SUCCESSFUL;
}

static int
irongate_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *(vuip)addr;
	return PCIBIOS_SUCCESSFUL;
}

static int
irongate_write_config_byte(struct pci_dev *dev, int where, u8 value)
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
irongate_write_config_word(struct pci_dev *dev, int where, u16 value)
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
irongate_write_config_dword(struct pci_dev *dev, int where, u32 value)
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


struct pci_ops irongate_pci_ops =
{
	read_byte:	irongate_read_config_byte,
	read_word:	irongate_read_config_word,
	read_dword:	irongate_read_config_dword,
	write_byte:	irongate_write_config_byte,
	write_word:	irongate_write_config_word,
	write_dword:	irongate_write_config_dword
};

#ifdef DEBUG_IRONGATE
static void
irongate_register_dump(const char *function_name)
{
	printk("%s: Irongate registers:\n"
	       "\tFunction 0:\n"
	       "\tdev_vendor\t0x%08x\n"
	       "\tstat_cmd\t0x%08x\n"
	       "\tclass\t\t0x%08x\n"
	       "\tlatency\t\t0x%08x\n"
	       "\tbar0\t\t0x%08x\n"
	       "\tbar1\t\t0x%08x\n"
	       "\tbar2\t\t0x%08x\n"
	       "\trsrvd0[0]\t0x%08x\n"
	       "\trsrvd0[1]\t0x%08x\n"
	       "\trsrvd0[2]\t0x%08x\n"
	       "\trsrvd0[3]\t0x%08x\n"
	       "\trsrvd0[4]\t0x%08x\n"
	       "\trsrvd0[5]\t0x%08x\n"
	       "\tcapptr\t\t0x%08x\n"
	       "\trsrvd1[0]\t0x%08x\n"
	       "\trsrvd1[1]\t0x%08x\n"
	       "\tbacsr10\t\t0x%08x\n"
	       "\tbacsr32\t\t0x%08x\n"
	       "\tbacsr54\t\t0x%08x\n"
	       "\trsrvd2[0]\t0x%08x\n"
	       "\tdrammap\t\t0x%08x\n"
	       "\tdramtm\t\t0x%08x\n"
	       "\tdramms\t\t0x%08x\n"
	       "\trsrvd3[0]\t0x%08x\n"
	       "\tbiu0\t\t0x%08x\n"
	       "\tbiusip\t\t0x%08x\n"
	       "\trsrvd4[0]\t0x%08x\n"
	       "\trsrvd4[1]\t0x%08x\n"
	       "\tmro\t\t0x%08x\n"
	       "\trsrvd5[0]\t0x%08x\n"
	       "\trsrvd5[1]\t0x%08x\n"
	       "\trsrvd5[2]\t0x%08x\n"
	       "\twhami\t\t0x%08x\n"
	       "\tpciarb\t\t0x%08x\n"
	       "\tpcicfg\t\t0x%08x\n"
	       "\trsrvd6[0]\t0x%08x\n"
	       "\trsrvd6[1]\t0x%08x\n"
	       "\trsrvd6[2]\t0x%08x\n"
	       "\trsrvd6[3]\t0x%08x\n"
	       "\trsrvd6[4]\t0x%08x\n"
	       "\tagpcap\t\t0x%08x\n"
	       "\tagpstat\t\t0x%08x\n"
	       "\tagpcmd\t\t0x%08x\n"
	       "\tagpva\t\t0x%08x\n"
	       "\tagpmode\t\t0x%08x\n"

	       "\n\tFunction 1:\n"
	       "\tdev_vendor:\t0x%08x\n"
	       "\tcmd_status:\t0x%08x\n"
	       "\trevid_etc :\t0x%08x\n"
	       "\thtype_etc :\t0x%08x\n"
	       "\trsrvd0[0] :\t0x%08x\n"
	       "\trsrvd0[1] :\t0x%08x\n"
	       "\tbus_nmbers:\t0x%08x\n"
	       "\tio_baselim:\t0x%08x\n"
	       "\tmem_bselim:\t0x%08x\n"
	       "\tpf_baselib:\t0x%08x\n"
	       "\trsrvd1[0] :\t0x%08x\n"
	       "\trsrvd1[1] :\t0x%08x\n"
	       "\tio_baselim:\t0x%08x\n"
	       "\trsrvd2[0] :\t0x%08x\n"
	       "\trsrvd2[1] :\t0x%08x\n"
	       "\tinterrupt :\t0x%08x\n",

	       function_name,
	       IRONGATE0->dev_vendor,
	       IRONGATE0->stat_cmd,
	       IRONGATE0->class,
	       IRONGATE0->latency,
	       IRONGATE0->bar0,
	       IRONGATE0->bar1,
	       IRONGATE0->bar2,
	       IRONGATE0->rsrvd0[0],
	       IRONGATE0->rsrvd0[1],
	       IRONGATE0->rsrvd0[2],
	       IRONGATE0->rsrvd0[3],
	       IRONGATE0->rsrvd0[4],
	       IRONGATE0->rsrvd0[5],
	       IRONGATE0->capptr,
	       IRONGATE0->rsrvd1[0],
	       IRONGATE0->rsrvd1[1],
	       IRONGATE0->bacsr10,
	       IRONGATE0->bacsr32,
	       IRONGATE0->bacsr54,
	       IRONGATE0->rsrvd2[0],
	       IRONGATE0->drammap,
	       IRONGATE0->dramtm,
	       IRONGATE0->dramms,
	       IRONGATE0->rsrvd3[0],
	       IRONGATE0->biu0,
	       IRONGATE0->biusip,
	       IRONGATE0->rsrvd4[0],
	       IRONGATE0->rsrvd4[1],
	       IRONGATE0->mro,
	       IRONGATE0->rsrvd5[0],
	       IRONGATE0->rsrvd5[1],
	       IRONGATE0->rsrvd5[2],
	       IRONGATE0->whami,
	       IRONGATE0->pciarb,
	       IRONGATE0->pcicfg,
	       IRONGATE0->rsrvd6[0],
	       IRONGATE0->rsrvd6[1],
	       IRONGATE0->rsrvd6[2],
	       IRONGATE0->rsrvd6[3],
	       IRONGATE0->rsrvd6[4],
	       IRONGATE0->agpcap,
	       IRONGATE0->agpstat,
	       IRONGATE0->agpcmd,
	       IRONGATE0->agpva,
	       IRONGATE0->agpmode,
	       IRONGATE1->dev_vendor,
	       IRONGATE1->stat_cmd,
	       IRONGATE1->class,
	       IRONGATE1->htype,
	       IRONGATE1->rsrvd0[0],
	       IRONGATE1->rsrvd0[1],
	       IRONGATE1->busnos,
	       IRONGATE1->io_baselim_regs,
	       IRONGATE1->mem_baselim,
	       IRONGATE1->pfmem_baselim,
	       IRONGATE1->rsrvd1[0],
	       IRONGATE1->rsrvd1[1],
	       IRONGATE1->io_baselim,
	       IRONGATE1->rsrvd2[0],
	       IRONGATE1->rsrvd2[1],
	       IRONGATE1->interrupt );
}
#else
#define irongate_register_dump(x)
#endif

int
irongate_pci_clr_err(void)
{
	unsigned int nmi_ctl=0;
	unsigned int IRONGATE_jd;

again:
	IRONGATE_jd = IRONGATE0->stat_cmd;
	printk("Iron stat_cmd %x\n", IRONGATE_jd);
	IRONGATE0->stat_cmd = IRONGATE_jd; /* write again clears error bits */
	mb();
	IRONGATE_jd = IRONGATE0->stat_cmd;  /* re-read to force write */

	IRONGATE_jd = IRONGATE0->dramms;
	printk("Iron dramms %x\n", IRONGATE_jd);
	IRONGATE0->dramms = IRONGATE_jd; /* write again clears error bits */
	mb();
	IRONGATE_jd = IRONGATE0->dramms;  /* re-read to force write */

	/* Clear ALI NMI */
        nmi_ctl = inb(0x61);
        nmi_ctl |= 0x0c;
        outb(nmi_ctl, 0x61);
        nmi_ctl &= ~0x0c;
        outb(nmi_ctl, 0x61);

	IRONGATE_jd = IRONGATE0->dramms;
	if (IRONGATE_jd & 0x300) goto again;

	return 0;
}

void __init
irongate_init_arch(void)
{
	struct pci_controler *hose;

	IRONGATE0->stat_cmd = IRONGATE0->stat_cmd & ~0x100;
	irongate_pci_clr_err();
	irongate_register_dump(__FUNCTION__);

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controler();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	/* This is for userland consumption.  For some reason, the 40-bit
	   PIO bias that we use in the kernel through KSEG didn't work for
	   the page table based user mappings.  So make sure we get the
	   43-bit PIO bias.  */
	hose->sparse_mem_base = 0;
	hose->sparse_io_base = 0;
	hose->dense_mem_base
	  = (IRONGATE_MEM & 0xffffffffff) | 0x80000000000;
	hose->dense_io_base
	  = (IRONGATE_IO & 0xffffffffff) | 0x80000000000;

	hose->sg_isa = hose->sg_pci = NULL;
	__direct_map_base = 0;
	__direct_map_size = 0xffffffff;
}
