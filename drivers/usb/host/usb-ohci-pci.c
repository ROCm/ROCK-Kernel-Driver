#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>  /* for in_interrupt() */
#undef DEBUG
#include <linux/usb.h>

#include "../core/hcd.h"
#include "usb-ohci.h"

#ifdef CONFIG_PMAC_PBOOK
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/pci-bridge.h>
#ifndef CONFIG_PM
#define CONFIG_PM
#endif
#endif

int __devinit
hc_add_ohci(struct pci_dev *dev, int irq, void *membase, unsigned long flags,
	    ohci_t **ohci, const char *name, const char *slot_name);
extern void hc_remove_ohci(ohci_t *ohci);
extern int hc_start (ohci_t * ohci, struct device *parent_dev);
extern int hc_reset (ohci_t * ohci);

/*-------------------------------------------------------------------------*/

/* Increment the module usage count, start the control thread and
 * return success. */

static struct pci_driver ohci_pci_driver;

static int __devinit
hc_found_ohci (struct pci_dev *dev, int irq,
	void *mem_base, const struct pci_device_id *id)
{
	u8 latency, limit;
	ohci_t * ohci;
	int ret;

	printk(KERN_INFO __FILE__ ": usb-%s, %s\n", dev->slot_name, dev->name);

	/* bad pci latencies can contribute to overruns */ 
	pci_read_config_byte (dev, PCI_LATENCY_TIMER, &latency);
	if (latency) {
		pci_read_config_byte (dev, PCI_MAX_LAT, &limit);
		if (limit && limit < latency) {
			dbg ("PCI latency reduced to max %d", limit);
			pci_write_config_byte (dev, PCI_LATENCY_TIMER, limit);
			latency = limit;
		}
	}

	ret = hc_add_ohci(dev, irq, mem_base, id->driver_data,
			   &ohci, ohci_pci_driver.name, dev->slot_name);

	if (ret == 0) {
		ohci->pci_latency = latency;

		if (hc_start (ohci, &ohci->ohci_dev->dev) < 0) {
			err ("can't start usb-%s", ohci->slot_name);
			hc_remove_ohci(ohci);
			return -EBUSY;
		}

#ifdef	DEBUG
		ohci_dump (ohci, 1);
#endif
	}

	return ret;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

/* controller died; cleanup debris, then restart */
/* must not be called from interrupt context */

static void hc_restart (ohci_t *ohci)
{
	int temp;
	int i;

	if (ohci->pci_latency)
		pci_write_config_byte (ohci->ohci_dev, PCI_LATENCY_TIMER, ohci->pci_latency);

	ohci->disabled = 1;
	ohci->sleeping = 0;
	if (ohci->bus->root_hub)
		usb_disconnect (&ohci->bus->root_hub);
	
	/* empty the interrupt branches */
	for (i = 0; i < NUM_INTS; i++) ohci->ohci_int_load[i] = 0;
	for (i = 0; i < NUM_INTS; i++) ohci->hcca->int_table[i] = 0;
	
	/* no EDs to remove */
	ohci->ed_rm_list [0] = NULL;
	ohci->ed_rm_list [1] = NULL;

	/* empty control and bulk lists */	 
	ohci->ed_isotail     = NULL;
	ohci->ed_controltail = NULL;
	ohci->ed_bulktail    = NULL;

	if ((temp = hc_reset (ohci)) < 0 || 
	    (temp = hc_start (ohci, &ohci->ohci_dev->dev)) < 0) {
		err ("can't restart usb-%s, %d", ohci->ohci_dev->slot_name, temp);
	} else
		dbg ("restart usb-%s completed", ohci->ohci_dev->slot_name);
}

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

/* configured so that an OHCI device is always provided */
/* always called with process context; sleeping is OK */

static int __devinit
ohci_pci_probe (struct pci_dev *dev, const struct pci_device_id *id)
{
	unsigned long mem_resource, mem_len;
	void *mem_base;
	int status;

	if (pci_enable_device(dev) < 0)
		return -ENODEV;

        if (!dev->irq) {
        	err("found OHCI device with no IRQ assigned. check BIOS settings!");
		pci_disable_device (dev);
   	        return -ENODEV;
        }
	
	/* we read its hardware registers as memory */
	mem_resource = pci_resource_start(dev, 0);
	mem_len = pci_resource_len(dev, 0);
	if (!request_mem_region (mem_resource, mem_len, ohci_pci_driver.name)) {
		dbg ("controller already in use");
		pci_disable_device (dev);
		return -EBUSY;
	}

	mem_base = ioremap_nocache (mem_resource, mem_len);
	if (!mem_base) {
		err("Error mapping OHCI memory");
		release_mem_region(mem_resource, mem_len);
		pci_disable_device (dev);
		return -EFAULT;
	}

	/* controller writes into our memory */
	pci_set_master (dev);

	status = hc_found_ohci (dev, dev->irq, mem_base, id);
	if (status < 0) {
		iounmap (mem_base);
		release_mem_region(mem_resource, mem_len);
		pci_disable_device (dev);
	}
	return status;
} 

/*-------------------------------------------------------------------------*/

/* may be called from interrupt context [interface spec] */
/* may be called without controller present */
/* may be called with controller, bus, and devices active */

static void __devexit
ohci_pci_remove (struct pci_dev *dev)
{
	ohci_t		*ohci = (ohci_t *) pci_get_drvdata(dev);
	void		*membase = ohci->regs;

	dbg ("remove %s controller usb-%s%s%s",
		hcfs2string (ohci->hc_control & OHCI_CTRL_HCFS),
		dev->slot_name,
		ohci->disabled ? " (disabled)" : "",
		in_interrupt () ? " in interrupt" : ""
		);

	hc_remove_ohci(ohci);

 	/* unmap the IO address space */
 	iounmap (membase);
 
	release_mem_region (pci_resource_start (dev, 0), pci_resource_len (dev, 0));
}


#ifdef	CONFIG_PM

/*-------------------------------------------------------------------------*/

static int
ohci_pci_suspend (struct pci_dev *dev, u32 state)
{
	ohci_t			*ohci = (ohci_t *) pci_get_drvdata(dev);
	unsigned long		flags;
	u16 cmd;

	if ((ohci->hc_control & OHCI_CTRL_HCFS) != OHCI_USB_OPER) {
		dbg ("can't suspend usb-%s (state is %s)", dev->slot_name,
			hcfs2string (ohci->hc_control & OHCI_CTRL_HCFS));
		return -EIO;
	}

	/* act as if usb suspend can always be used */
	info ("USB suspend: usb-%s", dev->slot_name);
	ohci->sleeping = 1;

	/* First stop processing */
  	spin_lock_irqsave (&usb_ed_lock, flags);
	ohci->hc_control &= ~(OHCI_CTRL_PLE|OHCI_CTRL_CLE|OHCI_CTRL_BLE|OHCI_CTRL_IE);
	writel (ohci->hc_control, &ohci->regs->control);
	writel (OHCI_INTR_SF, &ohci->regs->intrstatus);
	(void) readl (&ohci->regs->intrstatus);
  	spin_unlock_irqrestore (&usb_ed_lock, flags);

	/* Wait a frame or two */
	mdelay(1);
	if (!readl (&ohci->regs->intrstatus) & OHCI_INTR_SF)
		mdelay (1);
		
#ifdef CONFIG_PMAC_PBOOK
	if (_machine == _MACH_Pmac)
		disable_irq (ohci->irq);
	/* else, 2.4 assumes shared irqs -- don't disable */
#endif
	/* Enable remote wakeup */
	writel (readl(&ohci->regs->intrenable) | OHCI_INTR_RD, &ohci->regs->intrenable);

	/* Suspend chip and let things settle down a bit */
	ohci->hc_control = OHCI_USB_SUSPEND;
	writel (ohci->hc_control, &ohci->regs->control);
	(void) readl (&ohci->regs->control);
	mdelay (500); /* No schedule here ! */
	switch (readl (&ohci->regs->control) & OHCI_CTRL_HCFS) {
		case OHCI_USB_RESET:
			dbg("Bus in reset phase ???");
			break;
		case OHCI_USB_RESUME:
			dbg("Bus in resume phase ???");
			break;
		case OHCI_USB_OPER:
			dbg("Bus in operational phase ???");
			break;
		case OHCI_USB_SUSPEND:
			dbg("Bus suspended");
			break;
	}
	/* In some rare situations, Apple's OHCI have happily trashed
	 * memory during sleep. We disable it's bus master bit during
	 * suspend
	 */
	pci_read_config_word (dev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_MASTER;
	pci_write_config_word (dev, PCI_COMMAND, cmd);
#ifdef CONFIG_PMAC_PBOOK
	{
	   	struct device_node	*of_node;

		/* Disable USB PAD & cell clock */
		of_node = pci_device_to_OF_node (ohci->ohci_dev);
		if (of_node && _machine == _MACH_Pmac)
			pmac_call_feature(PMAC_FTR_USB_ENABLE, of_node, 0, 0);
	}
#endif
	return 0;
}

/*-------------------------------------------------------------------------*/

static int
ohci_pci_resume (struct pci_dev *dev)
{
	ohci_t		*ohci = (ohci_t *) pci_get_drvdata(dev);
	int		temp;
	unsigned long	flags;

	/* guard against multiple resumes */
	atomic_inc (&ohci->resume_count);
	if (atomic_read (&ohci->resume_count) != 1) {
		err ("concurrent PCI resumes for usb-%s", dev->slot_name);
		atomic_dec (&ohci->resume_count);
		return 0;
	}

#ifdef CONFIG_PMAC_PBOOK
	{
		struct device_node *of_node;

		/* Re-enable USB PAD & cell clock */
		of_node = pci_device_to_OF_node (ohci->ohci_dev);
		if (of_node && _machine == _MACH_Pmac)
			pmac_call_feature(PMAC_FTR_USB_ENABLE, of_node, 0, 1);
	}
#endif

	/* did we suspend, or were we powered off? */
	ohci->hc_control = readl (&ohci->regs->control);
	temp = ohci->hc_control & OHCI_CTRL_HCFS;

#ifdef DEBUG
	/* the registers may look crazy here */
	ohci_dump_status (ohci);
#endif

	/* Re-enable bus mastering */
	pci_set_master(ohci->ohci_dev);
	
	switch (temp) {

	case OHCI_USB_RESET:	// lost power
		info ("USB restart: usb-%s", dev->slot_name);
		hc_restart (ohci);
		break;

	case OHCI_USB_SUSPEND:	// host wakeup
	case OHCI_USB_RESUME:	// remote wakeup
		info ("USB continue: usb-%s from %s wakeup", dev->slot_name,
			(temp == OHCI_USB_SUSPEND)
				? "host" : "remote");
		ohci->hc_control = OHCI_USB_RESUME;
		writel (ohci->hc_control, &ohci->regs->control);
		(void) readl (&ohci->regs->control);
		mdelay (20); /* no schedule here ! */
		/* Some controllers (lucent) need a longer delay here */
		mdelay (15);
		temp = readl (&ohci->regs->control);
		temp = ohci->hc_control & OHCI_CTRL_HCFS;
		if (temp != OHCI_USB_RESUME) {
			err ("controller usb-%s won't resume", dev->slot_name);
			ohci->disabled = 1;
			return -EIO;
		}

		/* Some chips likes being resumed first */
		writel (OHCI_USB_OPER, &ohci->regs->control);
		(void) readl (&ohci->regs->control);
		mdelay (3);

		/* Then re-enable operations */
		spin_lock_irqsave (&usb_ed_lock, flags);
		ohci->disabled = 0;
		ohci->sleeping = 0;
		ohci->hc_control = OHCI_CONTROL_INIT | OHCI_USB_OPER;
		if (!ohci->ed_rm_list[0] && !ohci->ed_rm_list[1]) {
			if (ohci->ed_controltail)
				ohci->hc_control |= OHCI_CTRL_CLE;
			if (ohci->ed_bulktail)
				ohci->hc_control |= OHCI_CTRL_BLE;
		}
		writel (ohci->hc_control, &ohci->regs->control);
		writel (OHCI_INTR_SF, &ohci->regs->intrstatus);
		writel (OHCI_INTR_SF, &ohci->regs->intrenable);
		/* Check for a pending done list */
		writel (OHCI_INTR_WDH, &ohci->regs->intrdisable);	
		(void) readl (&ohci->regs->intrdisable);
		spin_unlock_irqrestore (&usb_ed_lock, flags);
#ifdef CONFIG_PMAC_PBOOK
		if (_machine == _MACH_Pmac)
			enable_irq (ohci->irq);
#endif
		if (ohci->hcca->done_head)
			dl_done_list (ohci, dl_reverse_done_list (ohci));
		writel (OHCI_INTR_WDH, &ohci->regs->intrenable); 
		writel (OHCI_BLF, &ohci->regs->cmdstatus); /* start bulk list */
		writel (OHCI_CLF, &ohci->regs->cmdstatus); /* start Control list */
		break;

	default:
		warn ("odd PCI resume for usb-%s", dev->slot_name);
	}

	/* controller is operational, extra resumes are harmless */
	atomic_dec (&ohci->resume_count);

	return 0;
}

#endif	/* CONFIG_PM */


/*-------------------------------------------------------------------------*/

static const struct pci_device_id __devinitdata ohci_pci_ids [] = { {

	/*
	 * AMD-756 [Viper] USB has a serious erratum when used with
	 * lowspeed devices like mice.
	 */
	vendor:		0x1022,
	device:		0x740c,
	subvendor:	PCI_ANY_ID,
	subdevice:	PCI_ANY_ID,

	driver_data:	OHCI_QUIRK_AMD756,

} , {

	/* handle any USB OHCI controller */
	class: 		((PCI_CLASS_SERIAL_USB << 8) | 0x10),
	class_mask: 	~0,

	/* no matter who makes it */
	vendor:		PCI_ANY_ID,
	device:		PCI_ANY_ID,
	subvendor:	PCI_ANY_ID,
	subdevice:	PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE (pci, ohci_pci_ids);

static struct pci_driver ohci_pci_driver = {
	name:		"usb-ohci",
	id_table:	&ohci_pci_ids [0],

	probe:		ohci_pci_probe,
	remove:		__devexit_p(ohci_pci_remove),

#ifdef	CONFIG_PM
	suspend:	ohci_pci_suspend,
	resume:		ohci_pci_resume,
#endif	/* PM */
};

 
/*-------------------------------------------------------------------------*/

static int __init ohci_hcd_init (void) 
{
	return pci_module_init (&ohci_pci_driver);
}

/*-------------------------------------------------------------------------*/

static void __exit ohci_hcd_cleanup (void) 
{	
	pci_unregister_driver (&ohci_pci_driver);
}

module_init (ohci_hcd_init);
module_exit (ohci_hcd_cleanup);

MODULE_LICENSE("GPL");
