/*
 *  linux/drivers/usb/usb-ohci-sa1111.c
 *
 *  The outline of this code was taken from Brad Parkers <brad@heeltoe.com>
 *  original OHCI driver modifications, and reworked into a cleaner form
 *  by Russell King <rmk@arm.linux.org.uk>.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/errno.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/assabet.h>
#include <asm/arch/badge4.h>
#include <asm/hardware/sa1111.h>

#include "usb-ohci.h"

int __devinit
hc_add_ohci(struct pci_dev *dev, int irq, void *membase, unsigned long flags,
	    ohci_t **ohci, const char *name, const char *slot_name);
extern void hc_remove_ohci(ohci_t *ohci);
extern int hc_start (ohci_t * ohci, struct device *parent_dev);
extern int hc_reset (ohci_t * ohci);


static ohci_t *sa1111_ohci;

static void __init sa1111_ohci_configure(void)
{
	unsigned int usb_rst = 0;

	printk(KERN_DEBUG __FILE__ 
	       ": starting SA-1111 OHCI USB Controller\n");

#ifdef CONFIG_SA1100_BADGE4
	if (machine_is_badge4())
		/* power the bus */
		badge4_set_5V(BADGE4_5V_USB, 1);
#endif

	if (machine_is_xp860() ||
	    machine_has_neponset() ||
	    machine_is_pfs168() ||
	    machine_is_badge4())
		usb_rst = USB_RESET_PWRSENSELOW | USB_RESET_PWRCTRLLOW;

	/*
	 * Configure the power sense and control lines.  Place the USB
	 * host controller in reset.
	 */
	USB_RESET = usb_rst | USB_RESET_FORCEIFRESET | USB_RESET_FORCEHCRESET;

	/*
	 * Now, carefully enable the USB clock, and take
	 * the USB host controller out of reset.
	 */
	SKPCR |= SKPCR_UCLKEN;
	udelay(11);
	USB_RESET = usb_rst;
}

static void __exit sa1111_ohci_unconfigure(void)
{
	printk(KERN_DEBUG __FILE__ 
	       ": stopping SA-1111 OHCI USB Controller\n");

	/*
	 * Put the USB host controller into reset.
	 */
	USB_RESET |= USB_RESET_FORCEIFRESET | USB_RESET_FORCEHCRESET;

	/*
	 * Stop the USB clock.
	 */
	SKPCR &= ~SKPCR_UCLKEN;

#ifdef CONFIG_SA1100_BADGE4
	if (machine_is_badge4())
		badge4_set_5V(BADGE4_5V_USB, 0);
#endif
}


static int __init sa1111_ohci_init(void)
{
	int ret;

	if (!sa1111)
		return -ENODEV;

	/*
	 * Request memory resources.
	 */
	if (!request_mem_region(_USB_OHCI_OP_BASE, _USB_EXTENT, "usb-ohci"))
		return -EBUSY;

	sa1111_ohci_configure();

	/*
	 * Initialise the generic OHCI driver.
	 */
	sa1111_ohci = 0;
	ret = hc_add_ohci(SA1111_FAKE_PCIDEV, NIRQHCIM,
			  (void *)&USB_OHCI_OP_BASE, 0, &sa1111_ohci,
			  "usb-ohci", "sa1111");

	if (ret || !sa1111_ohci) {
		sa1111_ohci = 0;
		sa1111_ohci_unconfigure();
		release_mem_region(_USB_OHCI_OP_BASE, _USB_EXTENT);
		return -EBUSY;
	}

	if (hc_start (sa1111_ohci, &sa1111->dev) < 0) {
		err ("can't start usb-%s", sa1111_ohci->slot_name);
		hc_remove_ohci (sa1111_ohci);
		sa1111_ohci = 0;
		sa1111_ohci_unconfigure();
		release_mem_region(_USB_OHCI_OP_BASE, _USB_EXTENT);
		return -EBUSY;
	}

	return 0;
}

static void __exit sa1111_ohci_exit(void)
{
	printk(KERN_DEBUG __FUNCTION__ ": cleaning up\n");

	if (sa1111_ohci) {
		hc_remove_ohci(sa1111_ohci);
		sa1111_ohci = 0;
	}

	sa1111_ohci_unconfigure();
	release_mem_region(_USB_OHCI_OP_BASE, _USB_EXTENT);

	printk(KERN_DEBUG __FUNCTION__ ": exiting\n");
}

module_init(sa1111_ohci_init);
module_exit(sa1111_ohci_exit);

MODULE_LICENSE("GPL");
