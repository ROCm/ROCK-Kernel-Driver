/* $Id: pci_schizo.c,v 1.3 2001/02/13 01:16:44 davem Exp $
 * pci_schizo.c: SCHIZO specific PCI controller support.
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/irq.h>

#include "pci_impl.h"

static int schizo_read_byte(struct pci_dev *dev, int where, u8 *value)
{
	/* IMPLEMENT ME */
}

static int schizo_read_word(struct pci_dev *dev, int where, u16 *value)
{
	/* IMPLEMENT ME */
}

static int schizo_read_dword(struct pci_dev *dev, int where, u32 *value)
{
	/* IMPLEMENT ME */
}

static int schizo_write_byte(struct pci_dev *dev, int where, u8 value)
{
	/* IMPLEMENT ME */
}

static int schizo_write_word(struct pci_dev *dev, int where, u16 value)
{
	/* IMPLEMENT ME */
}

static int schizo_write_dword(struct pci_dev *dev, int where, u32 value)
{
	/* IMPLEMENT ME */
}

static struct pci_ops schizo_ops = {
	schizo_read_byte,
	schizo_read_word,
	schizo_read_dword,
	schizo_write_byte,
	schizo_write_word,
	schizo_write_dword
};

static void __init schizo_scan_bus(struct pci_controller_info *p)
{
	/* IMPLEMENT ME */
}

static unsigned int __init schizo_irq_build(struct pci_controller_info *p,
					    struct pci_dev *pdev,
					    unsigned int ino)
{
	/* IMPLEMENT ME */
}

static void __init schizo_base_address_update(struct pci_dev *pdev, int resource)
{
	/* IMPLEMENT ME */
}

static void __init schizo_resource_adjust(struct pci_dev *pdev,
					  struct resource *res,
					  struct resource *root)
{
	/* IMPLEMENT ME */
}

static void schizo_pbm_init(struct pci_controller_info *p,
			    int prom_node, int is_pbm_a)
{
	/* IMPLEMENT ME */
}

void __init schizo_init(int node)
{
	struct linux_prom64_registers pr_regs[3];
	struct pci_controller_info *p;
	struct pci_iommu *iommu;
	u32 portid;
	int is_pbm_a, err;

	portid = prom_getintdefault(node, "portid", 0xff);

	spin_lock_irqsave(&pci_controller_lock, flags);
	for(p = pci_controller_root; p; p = p->next) {
		if (p->portid == portid) {
			spin_unlock_irqrestore(&pci_controller_lock, flags);
			is_pbm_a = (p->pbm_A.prom_node == 0);
			schizo_pbm_init(p, node, is_pbm_a);
			return;
		}
	}
	spin_unlock_irqrestore(&pci_controller_lock, flags);

	p = kmalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(p, 0, sizeof(*p));

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_A.iommu = iommu;

	iommu = kmalloc(sizeof(struct pci_iommu), GFP_ATOMIC);
	if (!iommu) {
		prom_printf("SCHIZO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(iommu, 0, sizeof(*iommu));
	p->pbm_B.iommu = iommu;

	spin_lock_irqsave(&pci_controller_lock, flags);
	p->next = pci_controller_root;
	pci_controller_root = p;
	spin_unlock_irqrestore(&pci_controller_lock, flags);

	p->portid = portid;
	p->index = pci_num_controllers++;
	p->scan_bus = schizo_scan_bus;
	p->irq_build = schizo_irq_build;
	p->base_address_update = schizo_base_address_update;
	p->resource_adjust = schizo_resource_adjust;
	p->pci_ops = &schizo_ops;

pbm_init:
	/* Three OBP regs:
	 * 1) PBM controller regs
	 * 2) Schizo front-end controller regs (same for both PBMs)
	 * 3) Unknown... (0x7ffec000000 and 0x7ffee000000 on Excalibur)
	 */
	err = prom_getproperty(node, "reg",
			       (char *)&pr_regs[0],
			       sizeof(pr_regs));
	if (err == 0 || err == -1) {
		prom_printf("SCHIZO: Fatal error, no reg property.\n");
		prom_halt();
	}

	/* XXX Read REG base, record in controller/pbm structures. */

	/* XXX Report controller to console. */

	/* XXX Setup pci_memspace_mask */

	/* XXX Init core controller and IOMMU */

	is_pbm_a = XXX; /* Figure out this test */
	schizo_pbm_init(p, node, is_pbm_a);
}
