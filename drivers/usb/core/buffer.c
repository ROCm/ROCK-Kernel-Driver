/*
 * DMA memory management for framework level HCD code (hc_driver)
 *
 * This implementation plugs in through generic "usb_bus" level methods,
 * and works with real PCI, or when "pci device == null" makes sense.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>


#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/usb.h>
#include "hcd.h"


/*
 * DMA-Coherent Buffers
 */

/* FIXME tune these based on pool statistics ... */
static const size_t	pool_max [HCD_BUFFER_POOLS] = {
	/* platforms without dma-friendly caches might need to
	 * prevent cacheline sharing...
	 */
	32,
	128,
	512,
	PAGE_SIZE / 2
	/* bigger --> allocate pages */
};


/* SETUP primitives */

/**
 * hcd_buffer_create - initialize buffer pools
 * @hcd: the bus whose buffer pools are to be initialized
 * Context: !in_interrupt()
 *
 * Call this as part of initializing a host controller that uses the pci dma
 * memory allocators.  It initializes some pools of dma-consistent memory that
 * will be shared by all drivers using that controller, or returns a negative
 * errno value on error.
 *
 * Call hcd_buffer_destroy() to clean up after using those pools.
 */
int hcd_buffer_create (struct usb_hcd *hcd)
{
	char		name [16];
	int 		i, size;

	for (i = 0; i < HCD_BUFFER_POOLS; i++) { 
		if (!(size = pool_max [i]))
			continue;
		snprintf (name, sizeof name, "buffer-%d", size);
		hcd->pool [i] = pci_pool_create (name, hcd->pdev,
				size, size, 0);
		if (!hcd->pool [i]) {
			hcd_buffer_destroy (hcd);
			return -ENOMEM;
		}
	}
	return 0;
}
EXPORT_SYMBOL (hcd_buffer_create);


/**
 * hcd_buffer_destroy - deallocate buffer pools
 * @hcd: the bus whose buffer pools are to be destroyed
 * Context: !in_interrupt()
 *
 * This frees the buffer pools created by hcd_buffer_create().
 */
void hcd_buffer_destroy (struct usb_hcd *hcd)
{
	int		i;

	for (i = 0; i < HCD_BUFFER_POOLS; i++) { 
		struct pci_pool		*pool = hcd->pool [i];
		if (pool) {
			pci_pool_destroy (pool);
			hcd->pool [i] = 0;
		}
	}
}
EXPORT_SYMBOL (hcd_buffer_destroy);


/* sometimes alloc/free could use kmalloc with SLAB_DMA, for
 * better sharing and to leverage mm/slab.c intelligence.
 */

void *hcd_buffer_alloc (
	struct usb_bus 		*bus,
	size_t			size,
	int			mem_flags,
	dma_addr_t		*dma
)
{
	struct usb_hcd		*hcd = bus->hcpriv;
	int 			i;

	for (i = 0; i < HCD_BUFFER_POOLS; i++) {
		if (size <= pool_max [i])
			return pci_pool_alloc (hcd->pool [i], mem_flags, dma);
	}
	return pci_alloc_consistent (hcd->pdev, size, dma);
}

void hcd_buffer_free (
	struct usb_bus 		*bus,
	size_t			size,
	void 			*addr,
	dma_addr_t		dma
)
{
	struct usb_hcd		*hcd = bus->hcpriv;
	int 			i;

	for (i = 0; i < HCD_BUFFER_POOLS; i++) {
		if (size <= pool_max [i]) {
			pci_pool_free (hcd->pool [i], addr, dma);
			return;
		}
	}
	pci_free_consistent (hcd->pdev, size, addr, dma);
}
