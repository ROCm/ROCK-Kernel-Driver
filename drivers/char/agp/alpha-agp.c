#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/machvec.h>
#include <asm/agp_backend.h>
#include "../../../arch/alpha/kernel/pci_impl.h"

#include "agp.h"

static struct page *alpha_core_agp_vm_nopage(struct vm_area_struct *vma,
					     unsigned long address,
					     int write_access)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	dma_addr_t dma_addr;
	unsigned long pa;
	struct page *page;

	dma_addr = address - vma->vm_start + agp->aperture.bus_base;
	pa = agp->ops->translate(agp, dma_addr);

	if (pa == (unsigned long)-EINVAL) return NULL;	/* no translation */
	
	/*
	 * Get the page, inc the use count, and return it
	 */
	page = virt_to_page(__va(pa));
	get_page(page);
	return page;
}

static struct aper_size_info_fixed alpha_core_agp_sizes[] =
{
	{ 0, 0, 0 }, /* filled in by alpha_core_agp_setup */
};

static struct gatt_mask alpha_core_agp_masks[] = {
	{ .mask = 0, .type = 0 },
};

struct vm_operations_struct alpha_core_agp_vm_ops = {
	.nopage = alpha_core_agp_vm_nopage,
};


static int alpha_core_agp_nop(void)
{
	/* just return success */
	return 0;
}

static int alpha_core_agp_fetch_size(void)
{
	return alpha_core_agp_sizes[0].size;
}

static int alpha_core_agp_configure(void)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	agp_bridge->gart_bus_addr = agp->aperture.bus_base;
	return 0;
}

static void alpha_core_agp_cleanup(void)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;

	agp->ops->cleanup(agp);
}

static void alpha_core_agp_tlbflush(agp_memory *mem)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	alpha_mv.mv_pci_tbi(agp->hose, 0, -1);
}

static unsigned long alpha_core_agp_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */
	return addr | agp_bridge->masks[0].mask;
}

static void alpha_core_agp_enable(u32 mode)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;

	agp->mode.lw = agp_collect_device_status(mode, agp->capability.lw);

	agp->mode.bits.enable = 1;
	agp->ops->configure(agp);

	agp_device_command(agp->mode.lw, 0);
}

static int alpha_core_agp_insert_memory(agp_memory *mem, off_t pg_start, 
					int type)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	int num_entries, status;
	void *temp;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;
	if ((pg_start + mem->page_count) > num_entries) return -EINVAL;

	status = agp->ops->bind(agp, pg_start, mem);
	mb();
	agp_bridge->tlb_flush(mem);

	return status;
}

static int alpha_core_agp_remove_memory(agp_memory *mem, off_t pg_start, 
					int type)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	int status;

	status = agp->ops->unbind(agp, pg_start, mem);
	agp_bridge->tlb_flush(mem);
	return status;
}


static struct agp_driver alpha_core_agp_driver = {
	.owner = THIS_MODULE,
};
	
struct agp_bridge_data alpha_core_agp_bridge = {
	.type			= ALPHA_CORE_AGP,
	.masks			= alpha_core_agp_masks,
	.aperture_sizes		= aper_size,
	.current_size		= aper_size,	/* only one entry */
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 1,
	.dev_private_data	= agp,
	.configure		= alpha_core_agp_configure,
	.fetch_size		= alpha_core_agp_fetch_size,
	.cleanup		= alpha_core_agp_cleanup,
	.tlb_flush		= alpha_core_agp_tlbflush,
	.mask_memory		= alpha_core_agp_mask_memory,
	.agp_enable		= alpha_core_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= alpha_core_agp_nop,
	.free_gatt_table	= alpha_core_agp_nop,
	.insert_memory		= alpha_core_agp_insert_memory,
	.remove_memory		= alpha_core_agp_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.mode			= agp->capability.lw,
	.cant_use_aperture	= 1,
	.vm_ops			= &alpha_core_agp_vm_ops,
};

int __init
alpha_core_agp_setup(void)
{
	alpha_agp_info *agp = alpha_mv.agp_info();
	struct pci_dev *pdev;	/* faked */
	struct aper_size_info_fixed *aper_size;

	if (!agp)
		return -ENODEV;
	if (agp->ops->setup(agp))
		return -ENODEV;

	/*
	 * Build the aperture size descriptor
	 */
	aper_size = alpha_core_agp_sizes;
	if (!aper_size)
		return -ENOMEM;
	aper_size->size = agp->aperture.size / (1024 * 1024);
	aper_size->num_entries = agp->aperture.size / PAGE_SIZE;
	aper_size->page_order = ffs(aper_size->num_entries / 1024) - 1;

	/*
	 * Build a fake pci_dev struct
	 */
	pdev = kmalloc(sizeof(struct pci_dev), GFP_KERNEL);
	if (!pdev)
		return -ENOMEM;
	pdev->vendor = 0xffff;
	pdev->device = 0xffff;
	pdev->sysdata = agp->hose;

	alpha_core_agp_bridge.dev = pdev;
	memcpy(agp_bridge, &alpha_core_agp_bridge,
			sizeof(struct agp_bridge_data));

	alpha_core_agp_driver.dev = pdev;
	agp_register_driver(&alpha_core_agp_driver);
	printk(KERN_INFO "Detected AGP on hose %d\n", agp->hose->index);
	return 0;
}

static int __init agp_alpha_core_init(void)
{
	if (alpha_mv.agp_info)
		return alpha_core_agp_setup();
	return -ENODEV;
}

static void __exit agp_alpha_core_cleanup(void)
{
	agp_unregister_driver(&alpha_core_agp_driver);
	/* no pci driver for core */
}

module_init(agp_alpha_core_init);
module_exit(agp_alpha_core_cleanup);

MODULE_AUTHOR("Jeff Wiedemeier <Jeff.Wiedemeier@hp.com>");
MODULE_LICENSE("GPL and additional rights");
