/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <asm/sn/sgi.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/simulator.h>

extern pciio_provider_t *pciio_to_provider_fns(vertex_hdl_t dev);

int
snia_badaddr_val(volatile void *addr, int len, volatile void *ptr)
{
	int ret = 0;
	volatile void *new_addr;

	switch (len) {
	case 4:
		new_addr = (void *) addr;
		ret = ia64_sn_probe_io_slot((long) new_addr, len, (void *) ptr);
		break;
	default:
		printk(KERN_WARNING
		       "snia_badaddr_val given len %x but supports len of 4 only\n",
		       len);
	}

	if (ret < 0)
		panic("snia_badaddr_val: unexpected status (%d) in probing",
		      ret);
	return (ret);

}

nasid_t
snia_get_console_nasid(void)
{
	extern nasid_t console_nasid;
	extern nasid_t master_baseio_nasid;

	if (console_nasid < 0) {
		console_nasid = ia64_sn_get_console_nasid();
		if (console_nasid < 0) {
// ZZZ What do we do if we don't get a console nasid on the hardware????
			if (IS_RUNNING_ON_SIMULATOR())
				console_nasid = master_baseio_nasid;
		}
	}
	return console_nasid;
}

nasid_t
snia_get_master_baseio_nasid(void)
{
	extern nasid_t master_baseio_nasid;
	extern char master_baseio_wid;

	if (master_baseio_nasid < 0) {
		master_baseio_nasid = ia64_sn_get_master_baseio_nasid();

		if (master_baseio_nasid >= 0) {
			master_baseio_wid =
			    WIDGETID_GET(KL_CONFIG_CH_CONS_INFO
					 (master_baseio_nasid)->memory_base);
		}
	}
	return master_baseio_nasid;
}

/*
 * XXX: should probably be called __sn2_pci_rrb_alloc
 * used by qla1280
 */

int
snia_pcibr_rrb_alloc(struct pci_dev *pci_dev,
		     int *count_vchan0, int *count_vchan1)
{
	vertex_hdl_t dev = PCIDEV_VERTEX(pci_dev);

	return pcibr_rrb_alloc(dev, count_vchan0, count_vchan1);
}

/* 
 * XXX: interface should be more like
 *
 *     int __sn2_pci_enable_bwswap(struct pci_dev *dev);
 *     void __sn2_pci_disable_bswap(struct pci_dev *dev);
 */
/* used by ioc4 ide */

pciio_endian_t
snia_pciio_endian_set(struct pci_dev * pci_dev,
		      pciio_endian_t device_end, pciio_endian_t desired_end)
{
	vertex_hdl_t dev = PCIDEV_VERTEX(pci_dev);

	return ((pciio_to_provider_fns(dev))->endian_set)
		(dev, device_end, desired_end);
}

EXPORT_SYMBOL(snia_pciio_endian_set);
EXPORT_SYMBOL(snia_pcibr_rrb_alloc);
