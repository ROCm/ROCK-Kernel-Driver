/*======================================================================

    A driver for CardBus serial devices

    serial_cb.c 1.20 2000/08/07 19:02:03

    Copyright 1998, 1999 by Donald Becker and David Hinds
    
    This software may be used and distributed according to the terms
    of the GNU Public License, incorporated herein by reference.
    All other rights reserved.
    
    This driver is an activator for CardBus serial cards, as
    found on multifunction (e.g. Ethernet and Modem) CardBus cards.
    
    Donald Becker may be reached as becker@CESDIS.edu, or C/O
    USRA Center of Excellence in Space Data and Information Sciences
    Code 930.5, NASA Goddard Space Flight Center, Greenbelt MD 20771
    David Hinds may be reached at dahinds@users.sourceforge.net
    
======================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/pci.h>
#include <asm/io.h>

#include <pcmcia/driver_ops.h>

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"serial_cb.c 1.20 2000/08/07 19:02:03 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*======================================================================

    Card-specific configuration hacks

======================================================================*/

static void device_setup(struct pci_dev *pdev, u_int ioaddr)
{
    u_short a, b;

    a = pdev->subsystem_vendor;
    b = pdev->subsystem_device;
    if (((a == 0x13a2) && (b == 0x8007)) ||
	((a == 0x1420) && (b == 0x8003))) {
	/* Ositech, Psion 83c175-based cards */
	DEBUG(0, "  83c175 NVCTL_m = 0x%4.4x.\n", inl(ioaddr+0x80));
	outl(0x4C00, ioaddr + 0x80);
	outl(0x4C80, ioaddr + 0x80);
    }
    DEBUG(0, "  modem registers are %2.2x %2.2x %2.2x "
	  "%2.2x %2.2x %2.2x %2.2x %2.2x  %2.2x.\n",
	  inb(ioaddr + 0), inb(ioaddr + 1), inb(ioaddr + 2),
	  inb(ioaddr + 3), inb(ioaddr + 4), inb(ioaddr + 5),
	  inb(ioaddr + 6), inb(ioaddr + 7), inb(ioaddr + 8));
}

/*======================================================================

    serial_attach() creates a serial device "instance" and registers
    it with the kernel serial driver, and serial_detach() unregisters
    an instance.

======================================================================*/

static dev_node_t *serial_attach(dev_locator_t *loc)
{
    u_int io;
    u_char irq;
    int line;
    struct serial_struct serial;
    struct pci_dev *pdev;
    dev_node_t *node;
    
    MOD_INC_USE_COUNT;

    if (loc->bus != LOC_PCI) goto err_out;
    pdev = pci_find_slot (loc->b.pci.bus, loc->b.pci.devfn);
    if (!pdev) goto err_out;
    if (pci_enable_device(pdev)) goto err_out;

    printk(KERN_INFO "serial_attach(bus %d, fn %d)\n", pdev->bus->number, pdev->devfn);
    io = pci_resource_start (pdev, 0);
    irq = pdev->irq;
    if (!(pci_resource_flags(pdev, 0) & IORESOURCE_IO)) {
	printk(KERN_NOTICE "serial_cb: PCI base address 0 is not IO\n");
	goto err_out;
    }
    device_setup(pdev, io);
    memset(&serial, 0, sizeof(serial));
    serial.port = io; serial.irq = irq;
    serial.flags = ASYNC_SKIP_TEST | ASYNC_SHARE_IRQ;

    /* Some devices seem to need extra time */
    __set_current_state(TASK_UNINTERRUPTIBLE);
    schedule_timeout(HZ/50);

    line = register_serial(&serial);
    if (line < 0) {
	printk(KERN_NOTICE "serial_cb: register_serial() at 0x%04x, "
	       "irq %d failed\n", serial.port, serial.irq);
    	goto err_out;
    }

    node = kmalloc(sizeof(dev_node_t), GFP_KERNEL);
    if (!node)
	    goto err_out_unregister;
    sprintf(node->dev_name, "ttyS%d", line);
    node->major = TTY_MAJOR; node->minor = 0x40 + line;
    node->next = NULL;
    return node;

err_out_unregister:
    unregister_serial(line);
err_out:
    MOD_DEC_USE_COUNT;
    return NULL;
}

static void serial_detach(dev_node_t *node)
{
    DEBUG(0, "serial_detach(ttyS%02d)\n", node->minor - 0x40);
    unregister_serial(node->minor - 0x40);
    kfree(node);
    MOD_DEC_USE_COUNT;
}

/*====================================================================*/

struct driver_operations serial_ops = {
    "serial_cb", serial_attach, NULL, NULL, serial_detach
};

static int __init init_serial_cb(void)
{
    DEBUG(0, "%s\n", version);
    register_driver(&serial_ops);
    return 0;
}

static void __exit exit_serial_cb(void)
{
    DEBUG(0, "serial_cb: unloading\n");
    unregister_driver(&serial_ops);
}

module_init(init_serial_cb);
module_exit(exit_serial_cb);
