/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 * 
 * OMAP Bus Glue
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Rusell King et al.
 *
 * Modified for OMAP from ohci-sa1111.c by Tony Lindgren <tony@atomide.com>
 * Based on the 2.4 OMAP OHCI driver originally done by MontaVista Software Inc.
 *
 * This file is licenced under the GPL.
 */
 
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/io.h>

#include <asm/arch/bus.h>
#include <asm/arch/hardware.h>
#include <asm/arch/mux.h>
#include <asm/arch/irqs.h>

#include "ohci-omap.h"

#ifndef CONFIG_ARCH_OMAP
#error "This file is OMAP bus glue.  CONFIG_OMAP must be defined."
#endif

extern int usb_disabled(void);
extern int ocpi_enable(void);

/*
 * Use the first port only by default. Override with hmc_mode option.
 *
 * NOTE: Many OMAP-1510 Innovators supposedly have bad wiring for the USB ports
 *       1 & 2, so only port 0 will work. To use the OHCI on the first port, use 
 *       the Innovator USB client cable with a client-to-client connector and modify
 *       either the cable or the hub to feed 5V VBUS back to Innovator. VBUS should
 *       be the red lead in the cable.
 *
 *       To mount USB hard disk as root, see the patch for do_mounts.c that tries 
 *       remounting the root, and use root=0801 if your root is on sda1. Does not 
 *       work with devfs.
 */
static int default_hmc_mode = 16;
static int hmc_mode = 1234;

/*
 * Set the USB host pin multiplexing and the selected HMC mode
 */
static int omap_usb_set_hmc_mode(int hmc_mode)
{
	unsigned int val;

	switch (hmc_mode) {
	case 0:
		/* 0: function, 1: disabled, 2: disabled */
		omap_cfg_reg(W4_USB_PUEN);
		omap_cfg_reg(R18_1510_USB_GPIO0);
		break;
	case 4:
		/* 0: function 1: host 2: host */
		omap_cfg_reg(usb1_speed);
		omap_cfg_reg(usb1_susp);
		omap_cfg_reg(usb1_seo);
		omap_cfg_reg(usb1_txen);
		omap_cfg_reg(usb1_txd);
		omap_cfg_reg(usb1_vp);
		omap_cfg_reg(usb1_vm);
		omap_cfg_reg(usb1_rcv);
		omap_cfg_reg(usb2_susp);
		omap_cfg_reg(usb2_seo);
		omap_cfg_reg(usb2_txen);
		omap_cfg_reg(usb2_txd);
		omap_cfg_reg(usb2_vp);
		omap_cfg_reg(usb2_vm);
		omap_cfg_reg(usb2_rcv);
		break;
	case 16:
		/* 0: host, 1: disabled, 2: disabled */
		omap_cfg_reg(W9_USB0_TXEN);
		omap_cfg_reg(AA9_USB0_VP);
		omap_cfg_reg(Y5_USB0_RCV);
		omap_cfg_reg(R9_USB0_VM);
		omap_cfg_reg(V6_USB0_TXD);
		omap_cfg_reg(W5_USB0_SE0);
		break;
	default:
		printk("Unknown USB host configuration: %i\n", hmc_mode);
		return -ENODEV;
	}

	/* Write the selected HMC mode */
	val = readl(MOD_CONF_CTRL_0) & ~HMC_CLEAR;
	val |= (hmc_mode << 1);
	writel(val, MOD_CONF_CTRL_0);

	return 0;
}

/*
 * OHCI clock initialization for OMAP-1510 and 1610
 */
static int omap_ohci_clock_power(int on)
{
	if (on) {
		if (cpu_is_omap_1510()) {
			/* Use DPLL, not APLL */
			writel(readl(ULPD_APLL_CTRL_REG) & ~APLL_NDPLL_SWITCH,
			       ULPD_APLL_CTRL_REG);

			/* Enable DPLL */
			writel(readl(ULPD_DPLL_CTRL_REG) | DPLL_PLL_ENABLE,
			       ULPD_DPLL_CTRL_REG);

			/* Software request for USB 48MHz clock */
			writel(readl(ULPD_SOFT_REQ_REG) | SOFT_REQ_REG_REQ,
			       ULPD_SOFT_REQ_REG);

			while (!(readl(ULPD_DPLL_CTRL_REG) & DPLL_LOCK));
		}

		if (cpu_is_omap_1610()) {
			/* Enable OHCI */
			writel(readl(ULPD_SOFT_REQ_REG) | SOFT_USB_OTG_REQ,
				ULPD_SOFT_REQ_REG);

			/* USB host clock request if not using OTG */
			writel(readl(ULPD_SOFT_REQ_REG) | SOFT_USB_REQ,
				ULPD_SOFT_REQ_REG);

			writel(readl(ULPD_STATUS_REQ_REG) | USB_HOST_DPLL_REQ,
			     ULPD_STATUS_REQ_REG);
		}

		/* Enable 48MHz clock to USB */
		writel(readl(ULPD_CLOCK_CTRL_REG) | USB_MCLK_EN,
		       ULPD_CLOCK_CTRL_REG);

		writel(readl(ARM_IDLECT2) | (1 << EN_LBFREECK) | (1 << EN_LBCK),
		       ARM_IDLECT2);

		writel(readl(MOD_CONF_CTRL_0) | USB_HOST_HHC_UHOST_EN,
		       MOD_CONF_CTRL_0);
	} else {
		/* Disable 48MHz clock to USB */
		writel(readl(ULPD_CLOCK_CTRL_REG) & ~USB_MCLK_EN,
		       ULPD_CLOCK_CTRL_REG);

		/* FIXME: The DPLL stays on for now */
	}

	return 0;
}

/*
 * Hardware specific transceiver power on/off
 */
static int omap_ohci_transceiver_power(int on)
{
	if (on) {
		if (omap_is_innovator())
			writel(readl(OMAP1510_FPGA_HOST_CTRL) | 0x20, 
			       OMAP1510_FPGA_HOST_CTRL);
	} else {
		if (omap_is_innovator())
			writel(readl(OMAP1510_FPGA_HOST_CTRL) & ~0x20, 
			       OMAP1510_FPGA_HOST_CTRL);
	}

	return 0;
}

/*
 * OMAP-1510 specific Local Bus clock on/off
 */
static int omap_1510_local_bus_power(int on)
{
	if (on) {
		writel((1 << 1) | (1 << 0), OMAP1510_LB_MMU_CTL);
		udelay(200);
	} else {
		writel(0, OMAP1510_LB_MMU_CTL);
	}

	return 0;
}

/*
 * OMAP-1510 specific Local Bus initialization
 * NOTE: This assumes 32MB memory size in OMAP1510LB_MEMSIZE.
 *       See also arch/mach-omap/memory.h for __virt_to_bus() and 
 *       __bus_to_virt() which need to match with the physical 
 *       Local Bus address below.
 */
static int omap_1510_local_bus_init(void)
{
	unsigned int tlb;
	unsigned long lbaddr, physaddr;

	writel((readl(OMAP1510_LB_CLOCK_DIV) & 0xfffffff8) | 0x4, 
	       OMAP1510_LB_CLOCK_DIV);

	/* Configure the Local Bus MMU table */
	for (tlb = 0; tlb < OMAP1510_LB_MEMSIZE; tlb++) {
		lbaddr = tlb * 0x00100000 + OMAP1510_LB_OFFSET;
		physaddr = tlb * 0x00100000 + PHYS_OFFSET;
		writel((lbaddr & 0x0fffffff) >> 22, OMAP1510_LB_MMU_CAM_H);
		writel(((lbaddr & 0x003ffc00) >> 6) | 0xc, 
		       OMAP1510_LB_MMU_CAM_L);
		writel(physaddr >> 16, OMAP1510_LB_MMU_RAM_H);
		writel((physaddr & 0x0000fc00) | 0x300, OMAP1510_LB_MMU_RAM_L);
		writel(tlb << 4, OMAP1510_LB_MMU_LCK);
		writel(0x1, OMAP1510_LB_MMU_LD_TLB);
	}

	/* Enable the walking table */
	writel(readl(OMAP1510_LB_MMU_CTL) | (1 << 3), OMAP1510_LB_MMU_CTL);
	udelay(200);

	return 0;
}

/*
 * OMAP-1610 specific hardware initialization
 *
 * Intended to configure OMAP-1610 USB host and OTG ports depending on 
 * the HMC mode selected.
 *
 * FIXME: Currently only supports alternate ping group 2 mode, should
 *        be easy to modify for other configurations once there is some
 *        hardware to test with.
 */
static int omap_1610_usb_init(int mode)
{
	u_int val = 0;

	/* Configure the OMAP transceiver settings */
	val |= (1 << 8); /* CONF_USB2_UNI TRM p 15-205*/
	val |= (4 << 4); /* TRM p 5-59, p 15-157 (1224) */

	//val |= (1 << 3); /* Isolate integrated transceiver from port 0 */
	val |= (1 << 2); /* Disable pulldown on integrated transceiver DM */
	val |= (1 << 1); /* Disable pulldown on integraded transceiver DP */

	writel(val, USB_TRANSCEIVER_CTRL);

	/* Set the USB0_TRX_MODE */
	val = 0;
	val &= ~OTG_IDLE_EN;
	val &= ~DEV_IDLE_EN;
	val &= ~(7 << 16);	/* Clear USB0_TRX_MODE */
	val |= (3 << 16);	/* 0 or 3, 6-wire DAT/SE0, TRM p 15-159 */
	writel(val, OTG_SYSCON_1);

	/* 
	 * Control via OTG, see TRM p 15-163
	 */
	val = 0;
	//val |= 1;		/* REVISIT: Enable OTG = 1 */

	/* Control via OTG */
	val &= ~HMC_PADEN;
	val &= ~OTG_PADEN;
	val |= UHOST_EN;	

	val &= ~0x3f;		/* Clear HMC mode */
	val |= mode;		/* Set HMC mode */
	val &= ~(7 << 16);	/* Clear ASE0_BRST */
	val |= (4 << 16);	/* Must be 4 */
	val |= USBX_SYNCHRO;	/* Must be set */
	val |= SRP_VBUS;
	writel(val, OTG_SYSCON_2);

	/* Enable OTG idle */
	//writel(readl(OTG_SYSCON_1) | OTG_IDLE_EN, OTG_SYSCON_1);

	return 0;
}

/*-------------------------------------------------------------------------*/

static void omap_start_hc(struct omap_dev *dev)
{
	printk(KERN_DEBUG __FILE__ 
	       ": starting OMAP OHCI USB Controller\n");

	/* 
	 * Set the HMC mode for the USB ports
	 */
#if 0
	/* See note about the Innovator wiring above */
	if (omap_is_innovator())
		hmc_mode = 4;	/* 0: function 1: host 2: host */
#endif

	if (cpu_is_omap_1610())
		ocpi_enable();

	omap_usb_set_hmc_mode(hmc_mode);

	omap_ohci_clock_power(1);
	omap_ohci_transceiver_power(1);

	if (cpu_is_omap_1510()) {
		omap_1510_local_bus_power(1);
		omap_1510_local_bus_init();
	}

	if (cpu_is_omap_1610())
		omap_1610_usb_init(hmc_mode);

	//omap_enable_device(dev);
}

static void omap_stop_hc(struct omap_dev *dev)
{
	printk(KERN_DEBUG __FILE__ 
	       ": stopping OMAP OHCI USB Controller\n");

	/*
	 * FIXME: Put the USB host controller into reset.
	 */

	/*
	 * FIXME: Stop the USB clock.
	 */
	//omap_disable_device(dev);

}


/*-------------------------------------------------------------------------*/

static irqreturn_t usb_hcd_omap_hcim_irq (int irq, void *__hcd, struct pt_regs * r)
{
	struct usb_hcd *hcd = __hcd;

	return usb_hcd_irq(irq, hcd, r);
}

/*-------------------------------------------------------------------------*/

void usb_hcd_omap_remove (struct usb_hcd *, struct omap_dev *);

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


/**
 * usb_hcd_omap_probe - initialize OMAP-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 * Store this function in the HCD's struct pci_driver as probe().
 */
int usb_hcd_omap_probe (const struct hc_driver *driver,
			  struct usb_hcd **hcd_out,
			  struct omap_dev *dev)
{
	int retval;
	struct usb_hcd *hcd = 0;

	if (!request_mem_region(dev->res.start, 
				dev->res.end - dev->res.start + 1, hcd_name)) {
		dbg("request_mem_region failed");
		return -EBUSY;
	}

	omap_start_hc(dev);

	hcd = driver->hcd_alloc ();
	if (hcd == NULL){
		dbg ("hcd_alloc failed");
		retval = -ENOMEM;
		goto err1;
	}

	hcd->driver = (struct hc_driver *) driver;
	hcd->description = driver->description;
	hcd->irq = dev->irq[0];
	hcd->regs = dev->mapbase;
	hcd->self.controller = &dev->dev;

	retval = hcd_buffer_create (hcd);
	if (retval != 0) {
		dbg ("pool alloc fail");
		goto err1;
	}

	retval = request_irq (hcd->irq, 
			      usb_hcd_omap_hcim_irq, 
			      SA_INTERRUPT, hcd->description, hcd);
	if (retval != 0) {
		dbg("request_irq failed");
		retval = -EBUSY;
		goto err2;
	}

	info ("%s (OMAP) at 0x%p, irq %d\n",
	      hcd->description, hcd->regs, hcd->irq);

	usb_bus_init (&hcd->self);
	hcd->self.op = &usb_hcd_operations;
	hcd->self.hcpriv = (void *) hcd;
	hcd->self.bus_name = "omap";
	hcd->product_desc = "OMAP OHCI";

	INIT_LIST_HEAD (&hcd->dev_list);
	usb_register_bus (&hcd->self);

	if ((retval = driver->start (hcd)) < 0) 
	{
		usb_hcd_omap_remove(hcd, dev);
		return retval;
	}

	*hcd_out = hcd;
	return 0;

 err2:
	hcd_buffer_destroy (hcd);
	if (hcd)
		driver->hcd_free(hcd);
 err1:
	omap_stop_hc(dev);

	release_mem_region(dev->res.start, dev->res.end - dev->res.start + 1);

	return retval;
}


/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_omap_remove - shutdown processing for OMAP-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_omap_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_hcd_omap_remove (struct usb_hcd *hcd, struct omap_dev *dev)
{
	void *base;

	info ("remove: %s, state %x", hcd->self.bus_name, hcd->state);

	if (in_interrupt ())
		BUG ();

	hcd->state = USB_STATE_QUIESCING;

	dbg ("%s: roothub graceful disconnect", hcd->self.bus_name);
	usb_disconnect (&hcd->self.root_hub);

	hcd->driver->stop (hcd);
	hcd_buffer_destroy (hcd);
	hcd->state = USB_STATE_HALT;

	free_irq (hcd->irq, hcd);

	usb_deregister_bus (&hcd->self);

	base = hcd->regs;
	hcd->driver->hcd_free (hcd);

	omap_stop_hc(dev);

	release_mem_region(dev->res.start, dev->res.end - dev->res.start + 1);
}

/*-------------------------------------------------------------------------*/

static int __devinit
ohci_omap_start (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

	ohci->hcca = dma_alloc_consistent (hcd->self.controller,
			sizeof *ohci->hcca, &ohci->hcca_dma);
	if (!ohci->hcca)
		return -ENOMEM;

        memset (ohci->hcca, 0, sizeof (struct ohci_hcca));
	if ((ret = ohci_mem_init (ohci)) < 0) {
		ohci_stop (hcd);
		return ret;
	}
	ohci->regs = hcd->regs;
	if (hc_reset (ohci) < 0) {
		ohci_stop (hcd);
		return -ENODEV;
	}

	if (hc_start (ohci) < 0) {
		err ("can't start %s", ohci->hcd.self.bus_name);
		ohci_stop (hcd);
		return -EBUSY;
	}
	create_debug_files (ohci);

#ifdef	DEBUG
	ohci_dump (ohci, 1);
#endif
	return 0;
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_omap_hc_driver = {
	.description =		hcd_name,

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_omap_start,
#ifdef	CONFIG_PM
	/* suspend:		ohci_omap_suspend,  -- tbd */
	/* resume:		ohci_omap_resume,   -- tbd */
#endif
	.stop =			ohci_stop,

	/*
	 * memory lifecycle (except per-request)
	 */
	.hcd_alloc =		ohci_hcd_alloc,
	.hcd_free =		ohci_hcd_free,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
};

/*-------------------------------------------------------------------------*/

static int ohci_hcd_omap_drv_probe(struct omap_dev *dev)
{
	struct usb_hcd *hcd = NULL;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_omap_probe(&ohci_omap_hc_driver, &hcd, dev);

	if (ret == 0)
		omap_set_drvdata(dev, hcd);

	return ret;
}

static int ohci_hcd_omap_drv_remove(struct omap_dev *dev)
{
	struct usb_hcd *hcd = omap_get_drvdata(dev);

	usb_hcd_omap_remove(hcd, dev);

	omap_set_drvdata(dev, NULL);

	return 0;
}

/*
 * Driver definition to register with the OMAP bus
 */
static struct omap_driver ohci_hcd_omap_driver = {
	.drv = {
		.name	= OMAP_OHCI_NAME,
	},
	.devid		= OMAP_OCP_DEVID_USB,
	.busid		= OMAP_BUS_OCP,
	.clocks		= 0,
	.probe		= ohci_hcd_omap_drv_probe,
	.remove		= ohci_hcd_omap_drv_remove,
};

/* Any dma_mask must be set for OHCI to work */
static u64 omap_dmamask = 0xffffffffUL;	

/*
 * Device definition to match the driver above
 */
static struct omap_dev ohci_hcd_omap_device = {
	.name		= OMAP_OHCI_NAME,
	.devid		= OMAP_OCP_DEVID_USB,
	.busid		= OMAP_BUS_OCP,
	.mapbase	= (void *)OMAP_OHCI_BASE,
	.dma_mask	= &omap_dmamask,	/* Needed only for OHCI */
	.res = {
		.start	= OMAP_OHCI_BASE,
		.end	= OMAP_OHCI_BASE + OMAP_OHCI_SIZE,
	},
	.irq = {
		INT_USB_HHC_1,
	},
};

static int __init ohci_hcd_omap_init (void)
{
	int ret;

	dbg (DRIVER_INFO " (OMAP)");
	dbg ("block sizes: ed %d td %d\n",
		sizeof (struct ed), sizeof (struct td));

	if (hmc_mode < 0 || hmc_mode > 25)
		hmc_mode = default_hmc_mode;

	/* Register the driver with OMAP bus */
	ret = omap_driver_register(&ohci_hcd_omap_driver);
	if (ret != 0)
		return -ENODEV;

	/* Register the device with OMAP bus */
	ret = omap_device_register(&ohci_hcd_omap_device);
	if (ret != 0) {
		omap_driver_unregister(&ohci_hcd_omap_driver);
		return -ENODEV;
	}

	return ret;
}

MODULE_PARM(hmc_mode, "hmc_mode");

static void __exit ohci_hcd_omap_cleanup (void)
{
	omap_device_unregister(&ohci_hcd_omap_device);
	omap_driver_unregister(&ohci_hcd_omap_driver);
}

module_init (ohci_hcd_omap_init);
module_exit (ohci_hcd_omap_cleanup);
